#!/usr/bin/env bash
# Test the fleet-queue-ingest raced-planning-gate reconcile (#2701).
#
# TASK-FILING.md §"Agent-approved follow-up lane" plan-shape 2 files in three
# non-atomic steps (create → post `## Plan` comment → add fleet:plan-review).
# The planning gate keys on the comment (#1932), so an ingest tick landing
# between steps 1 and 2 stamps fleet:needs-plan and step 3 lands
# fleet:plan-review on top. The pair used to persist forever — the
# already-labeled skip guard returns early on either label, so no later tick
# re-examined the issue. Ingest now strips the stale needs-plan and leaves the
# issue held by the plan-review arm of the guard.
#
# The discriminator is the label PAIR, not plan presence: fleet:needs-plan +
# human:review-plan is a LEGITIMATE state (a reviewer bounced the plan on a
# high-stakes issue; the human's approach gate persists) and must be left
# alone — observed live on #2698.
#
# HOME is redirected to a temp sandbox; gh is stubbed to canned surfaces and
# every `gh issue edit` is logged for assertions.

set -euo pipefail

source "$(dirname "$0")/lib_assert.sh"

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
INGEST="$SCRIPT_DIR/fleet-queue-ingest"
if [[ ! -x "$INGEST" ]]; then
    echo "test setup: fleet-queue-ingest not found at $INGEST" >&2
    exit 1
fi

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/state/projections" "$HOME/.fleet/logs"

PROJ="$HOME/.fleet/state/projections/queue-manager-ingest.json"
# #770 = needs-plan + plan-review (the race)     → strip needs-plan, still held
# #771 = needs-plan + human:review-plan          → legitimate bounce, untouched
# #772 = needs-plan alone                        → left for planning (untouched)
# #773 = plan-review alone                       → already correct, untouched
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":770,"repo":"engine"},
  {"number":771,"repo":"engine"},
  {"number":772,"repo":"engine"},
  {"number":773,"repo":"engine"}
],"unblock_issues":[]}
JSON

STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
export EDIT_LOG="$TMPROOT/edit.log"; : > "$EDIT_LOG"
export COMMENT_LOG="$TMPROOT/comment.log"; : > "$COMMENT_LOG"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    issue)
        case "$2" in
            view)
                case "$3" in
                    770) echo '{"title":"fleet: raced filing","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"fleet:agent-approved"},{"name":"fleet:needs-plan"},{"name":"fleet:plan-review"}],"comments":[{"body":"## Plan\n\nstep one"}]}' ;;
                    771) echo '{"title":"fleet: bounced high-stakes plan","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"fleet:agent-approved"},{"name":"fleet:needs-plan"},{"name":"human:review-plan"}],"comments":[{"body":"## Plan\n\nstep one"}]}' ;;
                    772) echo '{"title":"fleet: genuinely needs a plan","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:needs-plan"}],"comments":[]}' ;;
                    773) echo '{"title":"fleet: plan awaiting vetting","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:plan-review"}],"comments":[{"body":"## Plan\n\nstep one"}]}' ;;
                    *)   echo '{"title":"","body":"","labels":[],"comments":[]}' ;;
                esac
                exit 0 ;;
            edit)    printf '%s\n' "$*" >> "$EDIT_LOG"; exit 0 ;;
            comment) printf '%s\n' "$*" >> "$COMMENT_LOG"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
    pr)
        case "$2" in list) echo '[]'; exit 0 ;; *) exit 0 ;; esac ;;
    api) echo "gh: Not Found (HTTP 404)" >&2; exit 1 ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

echo "=== run fleet-queue-ingest ==="
INGEST_LOG="$TMPROOT/ingest.log"
bash "$INGEST" > "$INGEST_LOG" 2>&1 || true

edits_for() { grep -E "(^| )edit $1( |$)" "$EDIT_LOG" | tr '\n' '|'; }

# --- #770: the race → stale needs-plan stripped, plan-review hold preserved ---
assert_contains "$(edits_for 770)" "--remove-label fleet:needs-plan" \
    "#770 stripped the raced fleet:needs-plan (needs-plan + plan-review)"
assert_absent "$(edits_for 770)" "fleet:plan-review" \
    "#770 left fleet:plan-review in place (reviewer still owes a vet)"
assert_absent "$(edits_for 770)" "fleet:queued" \
    "#770 not queued — still held by the plan-review guard"

# --- #771: needs-plan + human:review-plan is legitimate → untouched -----------
assert_absent "$(edits_for 771)" "--remove-label fleet:needs-plan" \
    "#771 kept fleet:needs-plan (reviewer bounce on a high-stakes issue)"
assert_absent "$(edits_for 771)" "fleet:queued" \
    "#771 not queued (correctly skipped at the needs-plan guard)"

# --- #772: plain needs-plan → untouched --------------------------------------
assert_absent "$(edits_for 772)" "--remove-label fleet:needs-plan" \
    "#772 kept fleet:needs-plan (no plan-review present)"

# --- #773: plain plan-review → untouched -------------------------------------
assert_absent "$(edits_for 773)" "fleet:needs-plan" \
    "#773 untouched (plan-review alone is already the correct state)"
assert_absent "$(edits_for 773)" "fleet:queued" \
    "#773 not queued — held for plan review"

# --- the reconcile is observable in the run summary --------------------------
assert_contains "$(cat "$INGEST_LOG")" "raced needs-plan stripped 1" \
    "run summary counts the reconcile (1 issue)"

summarize "fleet-queue-ingest raced-planning-gate reconcile"

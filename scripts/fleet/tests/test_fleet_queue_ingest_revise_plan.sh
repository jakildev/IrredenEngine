#!/usr/bin/env bash
# Test that fleet-queue-ingest reconciles human:revise-plan issues back to
# fleet:needs-plan for a re-plan (the human-add-one-label affordance).
#
# A plan posted for a high-stakes issue sits in fleet:plan-review +
# human:review-plan awaiting agent + human sign-off. When the human wants the
# approach changed they add human:revise-plan (and a comment) — and NOTHING
# else. ingest must then, in one edit: add fleet:needs-plan, remove
# human:revise-plan and the now-stale fleet:plan-review, and KEEP human:approved
# + human:review-plan (so the human's approach gate persists across the re-plan
# and the issue can't queue behind the human's back). It must NOT stamp
# fleet:queued. A normal human:approved issue in the same batch must still be
# stamped, proving the harness can stamp and the reconcile is meaningful.
#
# HOME is redirected to a temp dir so the script's hardcoded projection/log/lock
# paths land in the sandbox, and `gh` is stubbed to canned issue/PR surfaces.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
INGEST="$SCRIPT_DIR/fleet-queue-ingest"

if [[ ! -x "$INGEST" ]]; then
    echo "test setup: fleet-queue-ingest not found at $INGEST" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

TMPROOT=$(mktemp -d)
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/state/projections" "$HOME/.fleet/logs"

PROJ="$HOME/.fleet/state/projections/queue-manager-ingest.json"
# #830 carries human:revise-plan on top of a mid-review plan (the scout's
# _ingest_skipped override is what lands it in pending_issues; here the
# projection is hand-built, so we list it directly). #831 is a normal approved
# issue (the stamp control).
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":830,"repo":"engine"},
  {"number":831,"repo":"engine"}
]}
JSON

# --- gh stub --------------------------------------------------------------
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
export EDIT_LOG="$TMPROOT/edit.log"; : > "$EDIT_LOG"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    issue)
        case "$2" in
            view)
                case "$3" in
                    830) echo '{"title":"t","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"fleet:task"},{"name":"human:approved"},{"name":"fleet:plan-review"},{"name":"human:review-plan"},{"name":"human:revise-plan"}],"comments":[{"body":"## Plan\nstep 1"}]}' ;;
                    831) echo '{"title":"t","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan\nstep 1"}]}' ;;
                    *)   echo '{"title":"","body":"","labels":[],"comments":[]}' ;;
                esac
                exit 0 ;;
            edit)
                printf '%s\n' "$*" >> "$EDIT_LOG"
                exit 0 ;;
            comment) exit 0 ;;
            *) exit 0 ;;
        esac ;;
    pr)
        case "$2" in
            list) echo '[]'; exit 0 ;;   # scope-shipped: no merged coverage
            *) exit 0 ;;
        esac ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

echo "=== run fleet-queue-ingest over a batch with one human:revise-plan issue ==="
bash "$INGEST" >/dev/null 2>&1 || true

# --- control: #831 normal approved must be stamped fleet:queued -----------
line_831=$(grep -E '(^| )831( |$)' "$EDIT_LOG" || true)
if [[ -n "$line_831" && "$line_831" == *"fleet:queued"* ]]; then
    ok "control #831 stamped fleet:queued (harness can stamp)"
else
    bad "control #831 was not stamped fleet:queued — harness broken, test vacuous"
fi

# --- #830 reconcile assertions -------------------------------------------
line_830=$(grep -E '(^| )830( |$)' "$EDIT_LOG" || true)
if [[ -z "$line_830" ]]; then
    bad "human:revise-plan #830 was never edited (reconcile didn't fire)"
else
    [[ "$line_830" == *"--add-label fleet:needs-plan"* ]] \
        && ok "#830 added fleet:needs-plan" \
        || bad "#830 did not add fleet:needs-plan: $line_830"
    [[ "$line_830" == *"--remove-label human:revise-plan"* ]] \
        && ok "#830 consumed human:revise-plan" \
        || bad "#830 did not remove human:revise-plan: $line_830"
    [[ "$line_830" == *"--remove-label fleet:plan-review"* ]] \
        && ok "#830 stripped stale fleet:plan-review" \
        || bad "#830 did not remove fleet:plan-review: $line_830"
    [[ "$line_830" != *"fleet:queued"* ]] \
        && ok "#830 never stamped fleet:queued" \
        || bad "#830 was stamped fleet:queued (must not queue on re-plan): $line_830"
    [[ "$line_830" != *"--remove-label human:review-plan"* ]] \
        && ok "#830 kept human:review-plan (human's approach gate persists)" \
        || bad "#830 removed human:review-plan (must persist): $line_830"
    [[ "$line_830" != *"--remove-label human:approved"* ]] \
        && ok "#830 kept human:approved (original triage)" \
        || bad "#830 removed human:approved: $line_830"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]

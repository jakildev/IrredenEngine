#!/usr/bin/env bash
# Test the fleet-queue-ingest retract pass (#2740).
#
# The three planning queue-blocks were enforced only at ingest STAMP time:
# nothing retracted an already-stamped fleet:queued when a gate landed
# afterwards. So a gate arriving after queuing held fleet-queue-ingest but NOT
# worker pickup — the issue stayed in the scout's pickable set and
# `fleet-claim claim` granted it (observed end-to-end on #2734). Ingest now
# strips fleet:queued from every retract candidate, and NEVER touches the gate
# label itself: the human/reviewer owns that, and clearing it re-queues the
# issue through the normal add path.
#
# Two gates, not three: human:review-plan was retired by PR #3112 and is inert
# (test_fleet_queue_ingest_review_plan_inert.sh pins that).
#
# Both arms below are load-bearing and neither is reachable from tasks.open:
#   - fleet:plan-review — fetch_task_queue `continue`s on it BEFORE building a
#     task row, so a tasks.open-derived candidate would never see it.
#   - a CLAIMED issue (fleet:claim-*) — routes to tasks.in_progress, and a gate
#     landing on a claimed issue is the #2734 shape itself.
#
# HOME is redirected to a temp sandbox; gh is stubbed to canned surfaces and
# every `gh issue edit` / `gh issue comment` is logged for assertions.

set -euo pipefail

source "$(dirname "$0")/lib_assert.sh"

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
INGEST="$SCRIPT_DIR/fleet-queue-ingest"
if [[ ! -x "$INGEST" ]]; then
    echo "SKIP: fleet-queue-ingest not found at $INGEST" >&2
    exit 3
fi

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/state/projections" "$HOME/.fleet/logs"

PROJ="$HOME/.fleet/state/projections/queue-manager-ingest.json"
# retract_issues:
#   #780 queued + fleet:needs-plan                → retract
#   #781 queued + fleet:plan-review               → retract  (constraint-1 arm)
#   #782 queued + fleet:plan-review + a claim     → retract, claim untouched
#                                                   (constraint-2 arm)
#   #783 gate already cleared live                → skip, no edit
#   #784 fleet:queued already gone live           → skip, no edit
# pending_issues:
#   #785 approved, planned, gate-free             → regression arm: still queued
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":785,"repo":"engine"}
],"unblock_issues":[],"retract_issues":[
  {"number":780,"repo":"engine"},
  {"number":781,"repo":"engine"},
  {"number":782,"repo":"engine"},
  {"number":783,"repo":"engine"},
  {"number":784,"repo":"engine"}
]}
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
                    780) echo '{"title":"fleet: gate landed after queuing","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"fleet:needs-plan"}],"comments":[]}' ;;
                    781) echo '{"title":"fleet: plan bounced after queuing","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"fleet:plan-review"}],"comments":[{"body":"## Plan\n\nstep one"}]}' ;;
                    782) echo '{"title":"fleet: gate landed on a claimed issue","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"fleet:plan-review"},{"name":"fleet:claim-mac-pool-3"}],"comments":[{"body":"## Plan\n\nstep one"}]}' ;;
                    783) echo '{"title":"fleet: reviewer cleared the gate first","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:queued"},{"name":"fleet:opus"}],"comments":[{"body":"## Plan\n\nstep one"}]}' ;;
                    784) echo '{"title":"fleet: another host retracted first","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:plan-review"}],"comments":[{"body":"## Plan\n\nstep one"}]}' ;;
                    785) echo '{"title":"fleet: ordinary approved task","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan\n\nstep one"}]}' ;;
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

echo "=== run fleet-queue-ingest over a projection carrying retract candidates ==="
INGEST_LOG="$TMPROOT/ingest.log"
bash "$INGEST" > "$INGEST_LOG" 2>&1 || true

edits_for()    { grep -E "(^| )edit $1( |$)" "$EDIT_LOG" | tr '\n' '|'; }
comments_for() { grep -E "(^| )comment $1( |$)" "$COMMENT_LOG" | tr '\n' '|'; }

# --- #780: fleet:needs-plan arm ---------------------------------------------
assert_contains "$(edits_for 780)" "--remove-label fleet:queued" \
    "#780 (needs-plan landed after queuing) had fleet:queued retracted"
assert_absent "$(edits_for 780)" "fleet:needs-plan" \
    "#780 gate label untouched — only the human/reviewer clears it"
assert_contains "$(comments_for 780)" "fleet:needs-plan" \
    "#780 got an explanatory comment naming the gate"

# --- #781: fleet:plan-review arm — INVISIBLE to a tasks.open-derived source --
assert_contains "$(edits_for 781)" "--remove-label fleet:queued" \
    "#781 (plan-review, dropped by fetch_task_queue before a task row exists) retracted"
assert_absent "$(edits_for 781)" "fleet:plan-review" \
    "#781 gate label untouched"

# --- #782: gate on a CLAIMED issue (tasks.in_progress, not tasks.open) -------
assert_contains "$(edits_for 782)" "--remove-label fleet:queued" \
    "#782 (gate landed under a live claim — the #2734 shape) retracted"
assert_absent "$(edits_for 782)" "fleet:claim-mac-pool-3" \
    "#782 claim label untouched — retract disturbs neither claim nor release"
assert_absent "$(edits_for 782)" "fleet:plan-review" \
    "#782 gate label untouched"

# --- #783: live re-check — gate already cleared → leave the queue stamp ------
assert_absent "$(edits_for 783)" "fleet:queued" \
    "#783 not retracted: the gate was cleared live, so the issue is correctly queued"

# --- #784: live re-check — another host already retracted -------------------
assert_absent "$(edits_for 784)" "fleet:queued" \
    "#784 not retracted twice: fleet:queued was already gone"

# --- #785: regression arm — an ordinary approved issue still queues ---------
assert_contains "$(edits_for 785)" "--add-label fleet:queued" \
    "#785 (gate-free, planned) still gets stamped fleet:queued"
assert_absent "$(edits_for 785)" "--remove-label fleet:queued" \
    "#785 never retracted"

# --- the retract is observable in the run summary ---------------------------
assert_contains "$(cat "$INGEST_LOG")" "retracted 3 queued-behind-a-gate" \
    "run summary counts the three retracts"
assert_contains "$(cat "$INGEST_LOG")" "(2 already reconciled)" \
    "run summary counts the two live-recheck skips"

summarize "fleet-queue-ingest planning-gate retract"

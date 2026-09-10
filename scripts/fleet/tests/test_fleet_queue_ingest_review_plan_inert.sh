#!/usr/bin/env bash
# Test that a straggler human:review-plan label is INERT to fleet-queue-ingest.
#
# human:review-plan (#2011) was the human approach-sign-off hold on a
# high-stakes worker-planned issue; it was retired 2026-09 (the plan
# reviewer's fleet:plan-review verdict is the only pre-queue gate now, and the
# label is deleted from the repo). An issue that still carries the label — a
# straggler re-applied by hand, or one that predates the deletion — must queue
# exactly like any other approved, planned issue: ingest must NOT hold on it.
# A normal human:approved issue in the same batch is the stamp control.
#
# HOME is redirected to a temp dir so the script's hardcoded projection/log/lock
# paths land in the sandbox, and `gh` is stubbed to canned issue/PR surfaces.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
INGEST="$SCRIPT_DIR/fleet-queue-ingest"

if [[ ! -x "$INGEST" ]]; then
    echo "test setup: fleet-queue-ingest not found at $INGEST" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/state/projections" "$HOME/.fleet/logs"

PROJ="$HOME/.fleet/state/projections/queue-manager-ingest.json"
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":820,"repo":"engine"},
  {"number":821,"repo":"engine"}
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
                # #820 carries a straggler human:review-plan (retired label; has a
                # ## Plan comment already); #821 is a normal approved issue.
                case "$3" in
                    820) echo '{"body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"human:review-plan"}],"comments":[{"body":"## Plan\nstep 1"}]}' ;;
                    821) echo '{"body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan\nstep 1"}]}' ;;
                    *)   echo '{"body":"","labels":[],"comments":[]}' ;;
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

echo "=== run fleet-queue-ingest over a batch with one straggler human:review-plan issue ==="
bash "$INGEST" >/dev/null 2>&1 || true

# #821 (normal approved) must be stamped fleet:queued.
if grep -qE '(^| )821( |$)' "$EDIT_LOG"; then
    ok "normal approved #821 was stamped (harness can stamp)"
else
    bad "normal approved #821 was NOT stamped — harness broken, skip test would be vacuous"
fi
if grep -q 'fleet:queued' "$EDIT_LOG" && grep -qE '(^| )821( |$)' "$EDIT_LOG"; then
    ok "#821 stamp carried fleet:queued"
else
    bad "#821 stamp missing fleet:queued"
fi

# #820 (straggler human:review-plan) must be stamped like any planned issue.
line_820=$(grep -E '(^| )820( |$)' "$EDIT_LOG" || true)
if [[ -n "$line_820" && "$line_820" == *"fleet:queued"* ]]; then
    ok "straggler human:review-plan #820 was stamped fleet:queued — the retired label is inert"
else
    bad "straggler human:review-plan #820 was NOT stamped fleet:queued (retired label still holds ingest): ${line_820:-<no edit>}"
fi

summarize "fleet-queue-ingest retired human:review-plan is inert"

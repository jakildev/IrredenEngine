#!/usr/bin/env bash
# Test that fleet-queue-ingest skips fleet:gated issues (#2762).
#
# A worker parks an issue fleet:gated when the fix surface is gated
# self-config no class can push (.claude/commands/role-*.md, .claude/agents/*,
# .claude/skills/**/SKILL.md). Because it keeps human:approved, it stays in
# the ingest pending set — so ingest must explicitly NOT re-stamp
# fleet:queued onto it, or the issue re-enters autonomous pickup and every
# matching dispatch re-claims, re-hits the ungateable surface, and releases
# (an unbounded pane-burn loop). A normal human:approved issue in the same
# batch must still be stamped, which proves the harness can stamp and the
# skip is meaningful (same shape as the human:owned / human:review-plan
# regression tests).
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
                # #830 carries fleet:gated (parked, gated fix surface);
                # #831 is a normal approved issue.
                case "$3" in
                    830) echo '{"body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:gated"}],"comments":[{"body":"## Plan\nstep 1"}]}' ;;
                    831) echo '{"body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan\nstep 1"}]}' ;;
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

echo "=== run fleet-queue-ingest over a batch with one fleet:gated issue ==="
bash "$INGEST" >/dev/null 2>&1 || true

# #831 (normal approved) must be stamped fleet:queued.
if grep -qE '(^| )831( |$)' "$EDIT_LOG"; then
    ok "normal approved #831 was stamped (harness can stamp)"
else
    bad "normal approved #831 was NOT stamped — harness broken, skip test would be vacuous"
fi
if grep -q 'fleet:queued' "$EDIT_LOG" && grep -qE '(^| )831( |$)' "$EDIT_LOG"; then
    ok "#831 stamp carried fleet:queued"
else
    bad "#831 stamp missing fleet:queued"
fi

# #830 (fleet:gated) must NOT be touched at all.
if grep -qE '(^| )830( |$)' "$EDIT_LOG"; then
    bad "fleet:gated #830 was edited (should have been skipped): $(grep 830 "$EDIT_LOG")"
else
    ok "fleet:gated #830 was skipped — never re-stamped fleet:queued"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]

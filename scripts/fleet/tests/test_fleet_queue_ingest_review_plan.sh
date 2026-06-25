#!/usr/bin/env bash
# Test that fleet-queue-ingest skips human:review-plan issues (#2011).
#
# A worker that plans a HIGH-STAKES needs-plan issue adds human:review-plan so
# the issue holds for the human's approach sign-off before implementation
# (distinct from fleet:plan-review's agent vetting). Because it keeps
# human:approved, it stays in the ingest pending set — so ingest must explicitly
# NOT re-stamp fleet:queued onto it until the human clears the label. A normal
# human:approved issue in the same batch must still be stamped, which proves the
# harness can stamp and the skip is meaningful (same shape as the human:owned
# regression test).
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
                # #820 carries human:review-plan (held for human sign-off, has a
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

echo "=== run fleet-queue-ingest over a batch with one human:review-plan issue ==="
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

# #820 (human:review-plan) must NOT be touched at all.
if grep -qE '(^| )820( |$)' "$EDIT_LOG"; then
    bad "human:review-plan #820 was edited (should have been skipped): $(grep 820 "$EDIT_LOG")"
else
    ok "human:review-plan #820 was skipped — never re-stamped fleet:queued"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]

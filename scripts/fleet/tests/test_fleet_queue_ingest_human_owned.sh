#!/usr/bin/env bash
# Test that fleet-queue-ingest skips human:owned issues (R6/C2 of #1357).
#
# A human can de-queue an issue by stamping human:owned (and removing
# fleet:queued). Because it keeps human:approved, it stays in the ingest
# pending set — so ingest must explicitly NOT re-stamp fleet:queued onto it.
# A normal human:approved issue in the same batch must still be stamped, which
# proves the harness can stamp and the skip is meaningful.
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
  {"number":710,"repo":"engine"},
  {"number":711,"repo":"engine"}
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
                # #710 is human-owned (de-queued); #711 is a normal approved issue.
                case "$3" in
                    710) echo '{"body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"human:owned"}]}' ;;
                    711) echo '{"body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}]}' ;;
                    *)   echo '{"body":"","labels":[]}' ;;
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

echo "=== run fleet-queue-ingest over a batch with one human:owned issue ==="
bash "$INGEST" >/dev/null 2>&1 || true

# #711 (normal approved) must be stamped fleet:queued.
if grep -qE '(^| )711( |$)' "$EDIT_LOG"; then
    ok "normal approved #711 was stamped (harness can stamp)"
else
    bad "normal approved #711 was NOT stamped — harness broken, skip test would be vacuous"
fi
if grep -q 'fleet:queued' "$EDIT_LOG" && grep -qE '(^| )711( |$)' "$EDIT_LOG"; then
    ok "#711 stamp carried fleet:queued"
else
    bad "#711 stamp missing fleet:queued"
fi

# #710 (human:owned) must NOT be touched at all.
if grep -qE '(^| )710( |$)' "$EDIT_LOG"; then
    bad "human:owned #710 was edited (should have been skipped): $(grep 710 "$EDIT_LOG")"
else
    ok "human:owned #710 was skipped — never re-stamped fleet:queued"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]

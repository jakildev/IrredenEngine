#!/usr/bin/env bash
# Test that fleet-queue-ingest honors `**Blocked by:**` (Gap 2 of #1476).
#
# An issue is stamped fleet:queued only when every `**Blocked by:** #N`
# predecessor is CLOSED/MERGED. A freshly-filed stacked epic must therefore
# show only its head queued, not every child at once. fleet-claim already
# enforces Blocked-by at claim time; this gate makes the LABEL honest.
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
# A stacked epic: #730 is the head (no blocker), #731 is blocked by an OPEN
# predecessor (#719), #732 is blocked by a CLOSED predecessor (#718).
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":730,"repo":"engine"},
  {"number":731,"repo":"engine"},
  {"number":732,"repo":"engine"}
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
                # Blocker-state probes pass `--jq .state`; the stamping fetch
                # asks for `--json body,labels`. Dispatch on which one this is.
                if [[ "$*" == *"--jq"* ]]; then
                    case "$3" in
                        718) echo "CLOSED" ;;   # #732's predecessor — satisfied
                        719) echo "OPEN" ;;     # #731's predecessor — still open
                        *)   echo "OPEN" ;;
                    esac
                    exit 0
                fi
                case "$3" in
                    730) echo '{"body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}]}' ;;
                    731) echo '{"body":"**Model:** sonnet\n**Blocked by:** #719","labels":[{"name":"human:approved"}]}' ;;
                    732) echo '{"body":"**Model:** opus\n**Blocked by:** #718","labels":[{"name":"human:approved"}]}' ;;
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

echo "=== run fleet-queue-ingest over a stacked epic (head + blocked + unblocked children) ==="
bash "$INGEST" >/dev/null 2>&1 || true

# #730 (head, no blocker) must be stamped fleet:queued.
if grep -qE '(^| )730( |$)' "$EDIT_LOG" && grep -q 'fleet:queued' "$EDIT_LOG"; then
    ok "head #730 (no blocker) was stamped fleet:queued"
else
    bad "head #730 was NOT stamped — gate is over-blocking or harness broken"
fi

# #732 (blocker CLOSED) must be stamped — a satisfied predecessor does not gate.
if grep -qE '(^| )732( |$)' "$EDIT_LOG"; then
    ok "#732 (predecessor #718 CLOSED) was stamped"
else
    bad "#732 was NOT stamped — gate wrongly blocked on a CLOSED predecessor"
fi

# #731 (blocker OPEN) must NOT be stamped — deferred until #719 closes.
if grep -qE '(^| )731( |$)' "$EDIT_LOG"; then
    bad "#731 was stamped despite OPEN predecessor #719: $(grep 731 "$EDIT_LOG")"
else
    ok "#731 (predecessor #719 OPEN) was deferred — not stamped"
fi

# The blocker issues themselves must never be edited (we only read their state).
if grep -qE '(^| )(718|719)( |$)' "$EDIT_LOG"; then
    bad "a blocker issue (#718/#719) was edited — gate must only READ blocker state"
else
    ok "blocker issues #718/#719 were never edited (read-only state probe)"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]

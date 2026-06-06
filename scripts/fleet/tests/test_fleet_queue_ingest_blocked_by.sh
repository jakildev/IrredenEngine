#!/usr/bin/env bash
# Test that fleet-queue-ingest queues blocked tasks with a fleet:blocked
# marker and removes the marker once the last blocker closes (#1527,
# supersedes the #1476 "defer the stamp until unblocked" behavior).
#
# Add path: every approved, non-skip task is stamped fleet:queued + model;
# a task whose **Blocked by:** predecessor is still open additionally gets
# fleet:blocked. Remove path: a queued task carrying fleet:blocked whose
# blocker has closed (surfaced in unblock_issues) has the marker stripped.
# The blocker issues are only READ (live state probe), never edited.
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
# Add path: a stacked epic. #730 = head (no blocker), #731 = blocked by an OPEN
# predecessor (#719), #732 = blocked by a CLOSED predecessor (#718).
# Cross-repo (#1522): #735 = engine task blocked by a CLOSED game ref
# (jakildev/irreden#777) → routed to game, not blocked; #736 = engine task
# blocked by an OPEN game ref (jakildev/irreden#778) → blocked.
# Remove path: #733 = queued+fleet:blocked, blocker #717 now CLOSED (unblock);
#              #734 = queued+fleet:blocked, blocker #719 still OPEN (stay).
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":730,"repo":"engine"},
  {"number":731,"repo":"engine"},
  {"number":732,"repo":"engine"},
  {"number":735,"repo":"engine"},
  {"number":736,"repo":"engine"}
],"unblock_issues":[
  {"number":733,"repo":"engine"},
  {"number":734,"repo":"engine"}
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
                # Blocker-state probes pass `--jq .state`; the body/labels fetch
                # asks for `--json body,labels`. Dispatch on which one this is.
                if [[ "$*" == *"--jq"* ]]; then
                    # Capture the --repo value so cross-repo refs (#1522) resolve
                    # against the referenced repo, not the issue's own.
                    bref_repo=""; bprev=""
                    for ba in "$@"; do
                        [[ "$bprev" == "--repo" ]] && bref_repo="$ba"; bprev="$ba"
                    done
                    case "$3" in
                        717) echo "CLOSED" ;;   # #733's predecessor — satisfied
                        718) echo "CLOSED" ;;   # #732's predecessor — satisfied
                        719) echo "OPEN" ;;     # #731/#734's predecessor — open
                        777)
                            # #1522: CLOSED only when routed to game (the
                            # referenced repo); OPEN if mis-routed to engine.
                            case "$bref_repo" in
                                jakildev/irreden) echo "CLOSED" ;;
                                *)                 echo "OPEN" ;;
                            esac ;;
                        778) echo "OPEN" ;;     # cross-repo game ref, still open
                        *)   echo "OPEN" ;;
                    esac
                    exit 0
                fi
                case "$3" in
                    730) echo '{"body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}]}' ;;
                    731) echo '{"body":"**Model:** sonnet\n**Blocked by:** #719","labels":[{"name":"human:approved"}]}' ;;
                    732) echo '{"body":"**Model:** opus\n**Blocked by:** #718","labels":[{"name":"human:approved"}]}' ;;
                    733) echo '{"body":"**Blocked by:** #717","labels":[{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"fleet:blocked"}]}' ;;
                    734) echo '{"body":"**Blocked by:** #719","labels":[{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"fleet:blocked"}]}' ;;
                    735) echo '{"body":"**Model:** opus\n**Blocked by:** jakildev/irreden#777","labels":[{"name":"human:approved"}]}' ;;
                    736) echo '{"body":"**Model:** opus\n**Blocked by:** jakildev/irreden#778","labels":[{"name":"human:approved"}]}' ;;
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

# Per-issue edit-log line (gh issue edit <N> ...) for assertions. Tolerates
# no-match (empty) without tripping `set -e` / pipefail.
edit_line() { grep -E "(^| )edit ${1}( |$)" "$EDIT_LOG" | head -1 || true; }

echo "=== run fleet-queue-ingest over a stacked epic + unblock candidates ==="
bash "$INGEST" >/dev/null 2>&1 || true

# --- Add path -------------------------------------------------------------
# #730 (head, no blocker) → fleet:queued, NO fleet:blocked.
l730=$(edit_line 730)
if [[ -n "$l730" && "$l730" == *"fleet:queued"* && "$l730" != *"fleet:blocked"* ]]; then
    ok "head #730 stamped fleet:queued without fleet:blocked"
else
    bad "head #730 mis-stamped: '$l730'"
fi

# #732 (blocker CLOSED) → fleet:queued, NO fleet:blocked.
l732=$(edit_line 732)
if [[ -n "$l732" && "$l732" == *"fleet:queued"* && "$l732" != *"fleet:blocked"* ]]; then
    ok "#732 (predecessor #718 CLOSED) stamped without fleet:blocked"
else
    bad "#732 mis-stamped: '$l732'"
fi

# #731 (blocker OPEN) → fleet:queued + fleet:blocked (queued, marked).
l731=$(edit_line 731)
if [[ -n "$l731" && "$l731" == *"fleet:queued"* && "$l731" == *"fleet:blocked"* ]]; then
    ok "#731 (predecessor #719 OPEN) stamped fleet:queued + fleet:blocked"
else
    bad "#731 not queued-with-marker as expected: '$l731'"
fi

# --- Cross-repo routing (#1522) -------------------------------------------
# #735 (cross-repo blocker game#777 CLOSED) → routed to game → NO fleet:blocked.
# If the gate mis-routed to engine, #777 would read OPEN and stamp fleet:blocked.
l735=$(edit_line 735)
if [[ -n "$l735" && "$l735" == *"fleet:queued"* && "$l735" != *"fleet:blocked"* ]]; then
    ok "#735 cross-repo blocker (game#777 CLOSED) routed to game → no fleet:blocked"
else
    bad "#735 cross-repo routing wrong (expected queued, no marker): '$l735'"
fi

# #736 (cross-repo blocker game#778 OPEN) → routed to game → fleet:blocked.
l736=$(edit_line 736)
if [[ -n "$l736" && "$l736" == *"fleet:queued"* && "$l736" == *"fleet:blocked"* ]]; then
    ok "#736 cross-repo blocker (game#778 OPEN) → fleet:queued + fleet:blocked"
else
    bad "#736 cross-repo open blocker not marked: '$l736'"
fi

# --- Remove path ----------------------------------------------------------
# #733 (blocker #717 now CLOSED) → fleet:blocked removed.
l733=$(edit_line 733)
if [[ -n "$l733" && "$l733" == *"--remove-label fleet:blocked"* ]]; then
    ok "#733 (predecessor #717 CLOSED) had fleet:blocked removed"
else
    bad "#733 fleet:blocked NOT removed despite closed blocker: '$l733'"
fi

# #734 (blocker #719 still OPEN) → fleet:blocked NOT removed (no edit).
l734=$(edit_line 734)
if [[ -z "$l734" ]]; then
    ok "#734 (predecessor #719 OPEN) left fleet:blocked in place (no edit)"
else
    bad "#734 fleet:blocked wrongly removed while blocker open: '$l734'"
fi

# --- Read-only blocker invariant -----------------------------------------
# The blocker issues themselves must never be edited (we only read their state),
# including the cross-repo blockers #777/#778 (#1522).
if grep -qE '(^| )edit (717|718|719|777|778)( |$)' "$EDIT_LOG"; then
    bad "a blocker issue (#717/#718/#719/#777/#778) was edited — must only READ blocker state"
else
    ok "blocker issues #717/#718/#719/#777/#778 were never edited (read-only state probe)"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]

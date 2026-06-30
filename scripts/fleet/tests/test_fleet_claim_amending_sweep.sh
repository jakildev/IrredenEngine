#!/usr/bin/env bash
# Tests for the host-qualified amending-sweep in `fleet-claim cleanup --gh`'s
# PR-label pass (#2099).
#
# The amending-sweep skips removing a past-TTL fleet:amending-<host>-<agent>
# label when the owning agent's heartbeat is fresh — an active worker is mid
# amend and sweeping its claim would trigger a duplicate dispatch (#1650). But
# heartbeats are HOST-LOCAL: ~/.fleet/heartbeats/<agent> is touched only by the
# worker on its own host. The old code keyed the freshness check by bare
# basename, so when the sweep ran on host B (mac) over a host-A (windows)
# amending label, it read *mac*'s heartbeat for the same basename. With a
# same-basename mac worker alive, the sweep saw a fresh heartbeat and skipped
# the stale cross-host label forever — the 32h #2089 deadlock.
#
# Fix: host-qualify the freshness skip. Only honor a fresh heartbeat for a
# SAME-HOST amending label; a cross-host label ages out on pure TTL.
#
# Every label here is reported as added long ago (events stub → 2020), so the
# age gate is always satisfied — the heartbeat host-qualification alone decides
# what survives.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }
assert_removed_contains() {
    if grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (not swept)"; fi
}
assert_removed_absent() {
    if ! grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (unexpectedly swept)"; fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
# Heartbeats live under $HOME/.fleet/heartbeats/<agent> (host-local, no env
# override) — isolate them in the sandbox.
export HOME="$TMPROOT/home"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR" \
    "$FLEET_ORPHANS_DIR" "$HOME/.fleet/heartbeats"

export PRS_JSON="$TMPROOT/prs.json"
export ISSUES_JSON="$TMPROOT/issues.json"
echo '[]' > "$ISSUES_JSON"   # no claim/steward labels to sweep in the later passes
REMOVED_FILE="$TMPROOT/removed.log"; : > "$REMOVED_FILE"; export REMOVED_FILE

STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    pr)
        case "$2" in
            list) cat "$PRS_JSON"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
    issue)
        case "$2" in
            list) cat "$ISSUES_JSON"; exit 0 ;;
            edit)
                shift 2; issue="$1"; shift
                while [[ $# -gt 0 ]]; do
                    case "$1" in
                        --remove-label) printf '%s\t%s\n' "$issue" "$2" >> "$REMOVED_FILE"; shift 2 ;;
                        *) shift ;;
                    esac
                done
                exit 0 ;;
            *) exit 0 ;;
        esac ;;
    api)
        # events endpoint — report every label as added long ago so the age
        # TTL is always elapsed; the heartbeat host-qualification alone decides.
        printf '%s ' "$@" | grep -q 'events' && echo "2020-01-01T00:00:00Z"
        exit 0 ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

# Live same-host workers. worker-1 collides (by basename) with the dead
# cross-host windows-worker-1 amending label below — the exact spoof geometry.
touch "$HOME/.fleet/heartbeats/worker-1"
touch "$HOME/.fleet/heartbeats/worker-2"
# (no heartbeat for worker-3 → its same-host claim is genuinely abandoned)

# 800 cross-host (windows) amending, basename collides with live mac-worker-1
#       -> swept (cannot observe windows's heartbeat; TTL governs) [the bug]
# 801 same-host (mac) amending, owner worker-2 heartbeat fresh
#       -> kept (live same-host owner)
# 802 same-host (mac) amending, owner worker-3 has no heartbeat
#       -> swept (abandoned)
# 803 cross-host (windows) reviewing — no heartbeat semantics
#       -> swept (pure TTL, unchanged)
cat > "$PRS_JSON" <<'JSON'
[
  {"number":800,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-windows-worker-1"}]},
  {"number":801,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-worker-2"}]},
  {"number":802,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-worker-3"}]},
  {"number":803,"labels":[{"name":"fleet:reviewing-windows-worker-1"}]}
]
JSON

echo "=== cleanup --gh host-qualified amending sweep ==="
OUT=$("$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1 || true)
echo "$OUT" | sed 's/^/    /'

assert_removed_contains $'800\tfleet:amending-windows-worker-1' \
    "cross-host amending label swept despite a fresh same-basename local heartbeat (#2099)"
assert_removed_absent   $'801\tfleet:amending-mac-worker-2' \
    "same-host amending label with a fresh owner heartbeat kept"
assert_removed_contains $'802\tfleet:amending-mac-worker-3' \
    "same-host amending label with no owner heartbeat swept (abandoned)"
assert_removed_contains $'803\tfleet:reviewing-windows-worker-1' \
    "cross-host reviewing label swept on pure TTL (no heartbeat semantics)"

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]

#!/usr/bin/env bash
# Tests for fleet-session-track — the SessionStart hook that keeps an
# architect's session sidecar (~/.fleet/sessions/<role>.session-id or the
# solo variant) pointing at the pane's LIVE conversation.
#
# The invariant under test: whatever session id Claude Code reports at
# session start (startup / resume / clear / compact) is what lands in the
# sidecar named by FLEET_SESSION_FILE, so `claude --resume $(cat sidecar)`
# always reloads the conversation the pane is actually in — the /clear
# case is the whole point (pre-hook, the sidecar pinned the first-ever
# UUID and every fleet-up resumed the pre-/clear conversation).
#
# Covers:
#   - no FLEET_SESSION_FILE            -> no-op (exit 0, nothing written)
#   - source=startup                   -> sidecar written with session_id
#   - source=clear                     -> sidecar REwritten + role-reminder
#                                         additionalContext emitted
#   - source=resume/compact            -> sidecar written, NO context output
#   - clear without FLEET_SESSION_ROLE -> sidecar written, NO context output
#   - malformed / empty payload        -> exit 0, sidecar untouched
#   - missing sidecar parent dir       -> created
#
# Hermetic: sidecars live in a mktemp dir via FLEET_SESSION_FILE; the hook
# never touches ~/.fleet on its own.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
TRACK="$SCRIPT_DIR/fleet-session-track"

if [[ ! -x "$TRACK" ]]; then
    echo "test setup: fleet-session-track not found at $TRACK" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

assert_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        '$needle' not found in: $haystack"
    fi
}

TMPROOT=$(mktemp -d -t fleet-session-track-test)

payload() {
    # $1 = source, $2 = session_id
    printf '{"hook_event_name":"SessionStart","source":"%s","session_id":"%s"}' "$1" "$2"
}

# --- T1: no FLEET_SESSION_FILE -> silent no-op -------------------------------
echo "T1: untracked session (no FLEET_SESSION_FILE) is a no-op"
rc=0
out=$(payload startup aaaa-1111 | env -u FLEET_SESSION_FILE "$TRACK") || rc=$?
assert_eq "$rc" "0" "exits 0"
assert_eq "$out" "" "emits nothing"

# --- T2: startup writes the sidecar ------------------------------------------
echo "T2: source=startup writes session_id to the sidecar"
F2="$TMPROOT/sessions/opus-architect.session-id"   # parent dir doesn't exist yet
out=$(payload startup bbbb-2222 | env FLEET_SESSION_FILE="$F2" "$TRACK")
assert_eq "$(cat "$F2")" "bbbb-2222" "sidecar holds the new session id"
assert_eq "$out" "" "no context output on startup"

# --- T3: clear repoints the sidecar and re-injects the role ------------------
echo "T3: source=clear repoints the sidecar + emits role reminder"
out=$(payload clear cccc-3333 | env FLEET_SESSION_FILE="$F2" \
    FLEET_SESSION_ROLE=opus-architect FLEET_SESSION_MODE=live "$TRACK")
assert_eq "$(cat "$F2")" "cccc-3333" "sidecar repointed to the post-/clear session"
assert_contains "$out" '"hookEventName": "SessionStart"' "context is SessionStart hook output"
assert_contains "$out" "role-opus-architect.md" "context names the role command file"
assert_contains "$out" "mode: live" "context carries the launch mode"

# --- T4: resume/compact write silently ---------------------------------------
echo "T4: source=resume and source=compact update silently"
for src in resume compact; do
    out=$(payload "$src" "dddd-$src" | env FLEET_SESSION_FILE="$F2" \
        FLEET_SESSION_ROLE=opus-architect "$TRACK")
    assert_eq "$(cat "$F2")" "dddd-$src" "sidecar updated on $src"
    assert_eq "$out" "" "no context output on $src"
done

# --- T5: clear with no role var still tracks, but stays silent ---------------
echo "T5: clear without FLEET_SESSION_ROLE tracks silently"
out=$(payload clear eeee-5555 | env -u FLEET_SESSION_ROLE \
    FLEET_SESSION_FILE="$F2" "$TRACK")
assert_eq "$(cat "$F2")" "eeee-5555" "sidecar still repointed"
assert_eq "$out" "" "no context without a role to name"

# --- T6: malformed payloads never break a session start ----------------------
echo "T6: malformed payloads exit 0 and leave the sidecar untouched"
before=$(cat "$F2")
for bad in 'not json' '{}' '{"source":"clear"}' '{"session_id":""}' '{"session_id":42}'; do
    rc=0
    out=$(printf '%s' "$bad" | env FLEET_SESSION_FILE="$F2" "$TRACK") || rc=$?
    assert_eq "$rc" "0" "exit 0 on payload: $bad"
    assert_eq "$out" "" "no output on payload: $bad"
done
assert_eq "$(cat "$F2")" "$before" "sidecar unchanged by malformed payloads"

echo
echo "passed: $PASS  failed: $FAIL"
[[ "$FAIL" -eq 0 ]]

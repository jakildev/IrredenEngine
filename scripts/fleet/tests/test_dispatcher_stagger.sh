#!/usr/bin/env bash
# Tests for the startup-burst / 429 hardening (dispatch stagger + short-window
# 429 backoff). Three pieces work together:
#
#   1. fleet-dispatcher's global min-gap (FLEET_DISPATCH_MIN_GAP_SECONDS) so a
#      single tick can't fire every idle pane at once — exercised through the
#      `--dispatch-gap-check <last-epoch> <now-epoch>` inspection subcommand.
#   2. fleet-dispatch-wrap classifying a non-zero exit + a throttle flag as a
#      short-window 429: writes `<pane>.throttle.ts` (short cooldown) and
#      re-arms the role's trigger — vs. the usage-limit exit (code 2 → `.ts`)
#      and the clean exit (no markers).
#   3. fleet-claude-stream touching FLEET_THROTTLE_FLAG when it sees the
#      "Server is temporarily limiting requests" line.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
WRAP="$SCRIPT_DIR/fleet-dispatch-wrap"
STREAM="$SCRIPT_DIR/fleet-claude-stream"

for f in "$DISPATCHER" "$WRAP" "$STREAM"; do
    if [[ ! -e "$f" ]]; then
        echo "test setup: missing $f" >&2
        exit 1
    fi
done

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

assert_file() {
    local path="$1" msg="$2"
    if [[ -f "$path" ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg (expected file to exist: $path)"
    fi
}

assert_no_file() {
    local path="$1" msg="$2"
    if [[ ! -f "$path" ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg (expected file to be absent: $path)"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_SESSION="fleet-test-$$"   # never touch a real fleet session

# --- Part 1: global min-gap math (--dispatch-gap-check) --------------------
echo "T1: dispatch min-gap gate (default 8s)"
assert_eq "$("$DISPATCHER" --dispatch-gap-check 100 104)" "wait" "Δ4 < 8 → wait"
assert_eq "$("$DISPATCHER" --dispatch-gap-check 100 107)" "wait" "Δ7 < 8 → wait"
assert_eq "$("$DISPATCHER" --dispatch-gap-check 100 108)" "ok"   "Δ8 == 8 → ok (boundary)"
assert_eq "$("$DISPATCHER" --dispatch-gap-check 100 120)" "ok"   "Δ20 > 8 → ok"

echo "T2: gap is configurable / disable-able"
assert_eq "$(FLEET_DISPATCH_MIN_GAP_SECONDS=0 "$DISPATCHER" --dispatch-gap-check 100 100)" \
    "ok" "gap=0 disables the stagger (always ok)"
assert_eq "$(FLEET_DISPATCH_MIN_GAP_SECONDS=30 "$DISPATCHER" --dispatch-gap-check 100 120)" \
    "wait" "gap=30, Δ20 → wait"
assert_eq "$(FLEET_DISPATCH_MIN_GAP_SECONDS=30 "$DISPATCHER" --dispatch-gap-check 100 130)" \
    "ok" "gap=30, Δ30 → ok"

echo "T3: --dispatch-gap-check arg validation"
if "$DISPATCHER" --dispatch-gap-check 100 >/dev/null 2>&1; then
    FAIL=$((FAIL + 1)); echo "  FAIL: missing now-epoch should exit non-zero"
else
    PASS=$((PASS + 1)); echo "  ok: missing now-epoch exits non-zero"
fi
if "$DISPATCHER" --dispatch-gap-check abc 100 >/dev/null 2>&1; then
    FAIL=$((FAIL + 1)); echo "  FAIL: non-integer epoch should exit non-zero"
else
    PASS=$((PASS + 1)); echo "  ok: non-integer epoch exits non-zero"
fi

# --- Part 2: fleet-dispatch-wrap exit-code classification ------------------
# Stub `claude` (chosen exit code) and `fleet-claude-stream` (optionally
# touches FLEET_THROTTLE_FLAG) on PATH so the wrapper runs end-to-end offline.
STUB_BIN="$TMPROOT/bin"
mkdir -p "$STUB_BIN"
cat >"$STUB_BIN/claude" <<'STUB'
#!/usr/bin/env bash
exit ${FAKE_CLAUDE_RC:-0}
STUB
cat >"$STUB_BIN/fleet-claude-stream" <<'STUB'
#!/usr/bin/env bash
cat >/dev/null   # drain claude's stdout
if [[ "${FAKE_STREAM_THROTTLE:-0}" == "1" && -n "${FLEET_THROTTLE_FLAG:-}" ]]; then
    touch "$FLEET_THROTTLE_FLAG"
fi
exit 0
STUB
chmod +x "$STUB_BIN/claude" "$STUB_BIN/fleet-claude-stream"

run_wrap() {
    # $1 rc, $2 throttle(0/1), $3 role; uses a fresh state dir each call.
    local rc="$1" throttle="$2" role="$3"
    WRAP_STATE=$(mktemp -d "$TMPROOT/state.XXXXXX")
    PATH="$STUB_BIN:$PATH" FLEET_STATE_DIR="$WRAP_STATE" \
        FAKE_CLAUDE_RC="$rc" FAKE_STREAM_THROTTLE="$throttle" \
        bash "$WRAP" pane-9 sonnet high "$role" >/dev/null 2>&1 || true
    echo "$WRAP_STATE"
}

echo "T4: short-window 429 (rc!=0 + throttle flag) → throttle cooldown + re-arm"
st=$(run_wrap 1 1 opus-reviewer)
assert_file    "$st/rate-limit/pane-9.throttle.ts" "429 writes the short throttle marker"
assert_file    "$st/triggers/opus-reviewer"        "429 re-arms the role trigger"
assert_no_file "$st/rate-limit/pane-9.ts"          "429 does NOT write the usage-limit marker"
assert_no_file "$st/rate-limit/pane-9.throttle-seen" "in-run throttle flag is cleaned up"

echo "T5: usage-limit exit (rc==2) → long cooldown marker only"
st=$(run_wrap 2 0 opus-reviewer)
assert_file    "$st/rate-limit/pane-9.ts"          "rc==2 writes the usage-limit marker"
assert_no_file "$st/rate-limit/pane-9.throttle.ts" "rc==2 does NOT write the throttle marker"
assert_no_file "$st/triggers/opus-reviewer"        "rc==2 does NOT re-arm (usage gate handles quota)"

echo "T6: clean exit (rc==0) → no markers, no re-arm, flag cleaned"
st=$(run_wrap 0 1 opus-reviewer)
assert_no_file "$st/rate-limit/pane-9.ts"            "clean exit writes no usage-limit marker"
assert_no_file "$st/rate-limit/pane-9.throttle.ts"   "clean exit writes no throttle marker"
assert_no_file "$st/triggers/opus-reviewer"          "clean exit does not re-arm a reviewer"
assert_no_file "$st/rate-limit/pane-9.throttle-seen" "clean exit cleans up the in-run flag"

echo "T7: non-zero exit WITHOUT a throttle flag → no throttle marker (generic failure)"
st=$(run_wrap 1 0 opus-reviewer)
assert_no_file "$st/rate-limit/pane-9.throttle.ts" "rc==1 w/o throttle flag → no throttle cooldown"
assert_no_file "$st/triggers/opus-reviewer"        "rc==1 w/o throttle flag → no re-arm"

# --- Part 3: fleet-claude-stream flag-touch on the 429 line ----------------
echo "T8: fleet-claude-stream touches FLEET_THROTTLE_FLAG on the 429 line"
flag="$TMPROOT/seen-429"
rm -f "$flag"
printf '%s\n' "API Error: Server is temporarily limiting requests (not your usage limit) · Rate limited" \
    | FLEET_THROTTLE_FLAG="$flag" python3 "$STREAM" >/dev/null 2>&1
assert_file "$flag" "throttle line touches the flag"

echo "T9: a normal line does NOT touch the flag"
flag2="$TMPROOT/seen-normal"
rm -f "$flag2"
printf '%s\n' "just some ordinary stderr output" \
    | FLEET_THROTTLE_FLAG="$flag2" python3 "$STREAM" >/dev/null 2>&1
assert_no_file "$flag2" "non-throttle line leaves the flag untouched"

echo
echo "passed: $PASS  failed: $FAIL"
[[ "$FAIL" -eq 0 ]]

#!/usr/bin/env bash
# Tests for ir-run's clean-exit RESULT reporting (engine/tools/bin/ir-run).
#
# The fleet's clean-exit policy (docs/agents/FLEET.md §"Clean-exit policy")
# keys off ir-run's one-line verdicts, so their shape and exit-code
# propagation are contract, not cosmetics:
#
#   RESULT=CLEAN exe=<name> exit=0                — ran + exited 0
#   RESULT=CRASH exe=<name> exit=<rc> signal=<..> — FAILED, code propagated
#   RESULT=ALIVE-TIMEOUT exe=<name> ...           — watchdog kill, healthy
#   RESULT=HOST-CLOSED exe=<name> exit=<rc> event=Application-Hang/1002 at=<t>
#                                                 — Windows hang handling
#                                                   closed it; re-run
#
# Hermetic: a fake build dir with tiny scripts stands in for real demos.
# The plain --timeout cases take no ir-acquire lock off Windows (on native
# Windows ir-run wraps every run); every case uses an isolated IR_LOCK_ROOT
# so none touches the host's real locks. Every case also runs the
# HOST-CLOSED check against stubbed seams (identity, clock, event query,
# one attempt), so none reads the host's Application log or sleeps between
# query attempts; the default query returns no events.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
IR_RUN="$REPO_ROOT/engine/tools/bin/ir-run"
IR_ACQUIRE="$REPO_ROOT/engine/tools/bin/ir-acquire"

# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$IR_RUN" ]]; then
    echo "test setup: ir-run not found at $IR_RUN" >&2
    exit 1
fi

FAKE_BUILD="$(mktemp -d)"
HOLDER_PID=""
release_holder() {
    if [[ -n "$HOLDER_PID" ]]; then
        kill "$HOLDER_PID" 2>/dev/null
        wait "$HOLDER_PID" 2>/dev/null
        HOLDER_PID=""
    fi
}
trap 'release_holder; rm -rf "$FAKE_BUILD"' EXIT
export IR_LOCK_ROOT="$FAKE_BUILD/locks"

make_exe() {
    local name="$1" body="$2"
    printf '#!/usr/bin/env bash\n%s\n' "$body" > "$FAKE_BUILD/$name"
    chmod +x "$FAKE_BUILD/$name"
}

make_exe clean-exit 'exit 0'
make_exe plain-fail 'exit 3'
# Real signal death (not `exit 139`): the reporter decodes 128+N.
make_exe segfaulter 'kill -SEGV $$'
make_exe sleeper 'sleep 30'
# exec so the watchdog's kill lands on the sleep itself, not a parent shell.
make_exe hang 'exec sleep 30'
make_exe short-run 'sleep 1; exit 0'
# The status both Windows bashes report for a hang-closed process.
make_exe exit127 'exit 127'

# --- HOST-CLOSED seams ------------------------------------------------------
# The launch identity is a fixed native pid + creation FILETIME; the clock
# stamps the child start, then its exit (10.5 s later); the query prints
# $HC_EVENTS from attempt $HC_FIRST_ATTEMPT on, logging its argv per attempt.
STUBS="$FAKE_BUILD/stubs"
mkdir -p "$STUBS"
printf '#!/usr/bin/env bash\necho "$HC_NATIVE_PID $HC_FILETIME"\n' > "$STUBS/identity"
printf '#!/usr/bin/env bash\nif [[ -e "$HC_CLOCK_STATE" ]]; then echo "$HC_END"; else : > "$HC_CLOCK_STATE"; echo "$HC_START"; fi\n' \
    > "$STUBS/clock"
printf '#!/usr/bin/env bash\necho "$*" >> "$HC_QUERY_LOG"\n(( $1 >= ${HC_FIRST_ATTEMPT:-1} )) && [[ -s "$HC_EVENTS" ]] && cat "$HC_EVENTS"\nexit 0\n' \
    > "$STUBS/query"
chmod +x "$STUBS/identity" "$STUBS/clock" "$STUBS/query"
export IR_RUN_HOST_CLOSE_IDENTITY="$STUBS/identity"
export IR_RUN_HOST_CLOSE_CLOCK="$STUBS/clock"
export IR_RUN_HOST_CLOSE_QUERY="$STUBS/query"
export IR_RUN_HOST_CLOSE_ATTEMPTS=1
export IR_RUN_HOST_CLOSE_INTERVAL=0
export HC_CLOCK_STATE="$STUBS/clock-state"
export HC_QUERY_LOG="$STUBS/query.log"
export HC_EVENTS="$STUBS/events.xml"
export HC_NATIVE_PID=4660                         # 0x1234
export HC_FILETIME=134348514173385979             # 0x01dd4d430767fcfb
export HC_START=1790337600.000                    # 2026-09-25T12:00:00Z
export HC_END=1790337610.500                      # 2026-09-25T12:00:10.5Z
HC_START_HEX=01dd4d430767fcfb
# Accepted event times: [max(start, end - 3), end + 1].
HC_TIME_CURRENT=2026-09-25T12:00:10.3757442Z      # end - 0.125, as measured
FIXTURE="$SCRIPT_DIR/fixtures/application_hang_1002.xml"

native_path() {
    local p
    p="$(cd "$FAKE_BUILD" && pwd)/$1"
    cygpath -w "$p" 2>/dev/null || echo "$p"
}

# event <exe> <native-path> <pid-hex> <start-hex> <SystemTime>: one fixture
# event with its identity fields filled in.
event() {
    local xml
    xml="$(<"$FIXTURE")"
    xml="${xml//@EXE@/"$1"}"
    xml="${xml//@PATH@/"$2"}"
    xml="${xml//@PID_HEX@/"$3"}"
    xml="${xml//@START_HEX@/"$4"}"
    xml="${xml//@TIME@/"$5"}"
    printf '%s\n' "$xml"
}

set_events() {
    printf '%s' "$1" > "$HC_EVENTS"
}

run_ir() {
    # Runs ir-run with the fake build dir; captures combined output and rc.
    rm -f "$HC_CLOCK_STATE" "$HC_QUERY_LOG"
    OUT="$("$IR_RUN" --build-dir "$FAKE_BUILD" "$@" 2>&1)"
    RC=$?
}

echo "clean exit before timeout:"
run_ir --timeout 10 clean-exit
assert_eq "$RC" "0" "clean exit propagates rc 0"
assert_contains "$OUT" "RESULT=CLEAN exe=clean-exit exit=0" "CLEAN verdict line"

echo "plain non-zero exit:"
run_ir --timeout 10 plain-fail
assert_eq "$RC" "1" "crash exits 1 in timeout mode"
assert_contains "$OUT" "RESULT=CRASH exe=plain-fail exit=3 signal=none" \
    "CRASH verdict names the code, signal=none"
assert_contains "$OUT" "clean-exit policy" "failure text cites the policy"

echo "signal death:"
run_ir --timeout 10 segfaulter
assert_eq "$RC" "1" "signal death exits 1 in timeout mode"
assert_contains "$OUT" "RESULT=CRASH exe=segfaulter exit=139 signal=SIGSEGV" \
    "CRASH verdict decodes SIGSEGV from 139"

echo "watchdog kill of a healthy process:"
run_ir --timeout 1 sleeper
assert_eq "$RC" "0" "watchdog kill stays exit 0 (healthy for smoke)"
assert_contains "$OUT" "RESULT=ALIVE-TIMEOUT exe=sleeper" "ALIVE-TIMEOUT verdict"

hold_gpu_lock() {
    # Holds the gpu lock from a second process for $1 seconds; returns once
    # the lock dir exists so the caller is guaranteed to queue behind it.
    "$IR_ACQUIRE" gpu -- sleep "$1" &
    HOLDER_PID=$!
    local i
    for i in $(seq 1 50); do
        [[ -s "$IR_LOCK_ROOT/gpu/lock/pid" ]] && return 0
        sleep 0.1
    done
    bad "test setup: gpu lock holder never acquired the lock"
}

echo "watchdog budget excludes time queued on the lock:"
hold_gpu_lock 4
run_ir --timeout 2 short-run --auto-screenshot 1
release_holder
assert_eq "$RC" "0" "run queued longer than --timeout still exits 0"
assert_contains "$OUT" "RESULT=CLEAN exe=short-run exit=0" \
    "queued run reports CLEAN, not ALIVE-TIMEOUT"
assert_absent "$OUT" "ALIVE-TIMEOUT exe=short-run" \
    "no watchdog verdict for a run that fits its budget"

echo "watchdog still fires after the lock is granted:"
run_ir --timeout 1 hang --auto-screenshot 1
assert_eq "$RC" "0" "watchdog kill of a wrapped run stays exit 0"
assert_contains "$OUT" "RESULT=ALIVE-TIMEOUT exe=hang" \
    "wrapped run that outlives --timeout reports ALIVE-TIMEOUT"
if [[ -d "$IR_LOCK_ROOT/gpu/lock" ]]; then
    bad "gpu lock released after the watchdog kill"
else
    ok "gpu lock released after the watchdog kill"
fi

echo "lock wait stays bounded by ir-acquire's own timeout:"
hold_gpu_lock 8
OUT="$(IR_QUEUE_TIMEOUT=1 "$IR_RUN" --build-dir "$FAKE_BUILD" --timeout 30 \
    short-run --auto-screenshot 1 2>&1)"
RC=$?
release_holder
assert_eq "$RC" "1" "lock never granted exits 1"
assert_contains "$OUT" "timeout waiting for lock lock (gpu, 1s)" \
    "failure names the lock ir-acquire could not get"
assert_contains "$OUT" "RESULT=LOCK-FAILED exe=short-run" \
    "LOCK-FAILED verdict, not a watchdog verdict"
assert_absent "$OUT" "ALIVE-TIMEOUT" "lock failure is not reported as ALIVE-TIMEOUT"

echo "verb-less run and the host's gpu-lock default:"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        run_ir --timeout 10 clean-exit
        assert_contains "$OUT" "ir-acquire gpu --"             "native Windows wraps a verb-less run in the gpu lock"
        OUT="$(IR_RUN_GL_EXCLUSIVE=0 "$IR_RUN" --build-dir "$FAKE_BUILD"             --timeout 10 clean-exit 2>&1)"
        RC=$?
        assert_absent "$OUT" "ir-acquire gpu"             "IR_RUN_GL_EXCLUSIVE=0 opts a verb-less run out of the lock"
        assert_contains "$OUT" "RESULT=CLEAN exe=clean-exit exit=0"             "opted-out run still reports CLEAN"
        ;;
    *)
        run_ir --timeout 10 clean-exit
        assert_absent "$OUT" "ir-acquire gpu"             "verb-less run takes no lock off native Windows"
        ;;
esac

# --- HOST-CLOSED: an Application Hang/1002 event naming this process --------
P127="$(native_path exit127)"
PSEGV="$(native_path segfaulter)"
MATCH_127="$(event exit127.exe "$P127" 1234 "$HC_START_HEX" "$HC_TIME_CURRENT")"
DECOY="$(event other.exe "$(native_path other.exe)" 1234 "$HC_START_HEX" "$HC_TIME_CURRENT")"

echo "host-closed: exact path/pid/creation/time match at exit 127:"
set_events "$DECOY$MATCH_127"
run_ir --timeout 10 exit127
assert_eq "$RC" "1" "host-closed early exit still exits 1 in timeout mode"
assert_contains "$OUT" \
    "RESULT=HOST-CLOSED exe=exit127 exit=127 event=Application-Hang/1002 at=$HC_TIME_CURRENT" \
    "HOST-CLOSED verdict names the rc, the event, and its time"
assert_contains "$OUT" "not a demo failure and not a green run" "directive says re-run, not green"
assert_absent "$OUT" "RESULT=CRASH" "no CRASH verdict for a host closure"

echo "host-closed: verb path propagates the child rc:"
run_ir exit127 --auto-screenshot 1
assert_eq "$RC" "127" "verb path propagates 127"
assert_contains "$OUT" "RESULT=HOST-CLOSED exe=exit127 exit=127" "verb path reports HOST-CLOSED"

echo "host-closed: the event wins over a signal exit:"
set_events "$(event segfaulter.exe "$PSEGV" 1234 "$HC_START_HEX" "$HC_TIME_CURRENT")"
run_ir --timeout 10 segfaulter
assert_contains "$OUT" "RESULT=HOST-CLOSED exe=segfaulter exit=139" "exact event at exit 139 is HOST-CLOSED"
assert_absent "$OUT" "RESULT=CRASH" "no CRASH verdict beside it"

echo "host-closed: no event stays CRASH:"
set_events ""
run_ir --timeout 10 exit127
assert_contains "$OUT" "RESULT=CRASH exe=exit127 exit=127 signal=none" "empty query is CRASH"
assert_absent "$OUT" "HOST-CLOSED" "no HOST-CLOSED without an event"

# assert_crash <label>: the current events must leave exit127 a CRASH.
assert_crash() {
    run_ir --timeout 10 exit127
    assert_contains "$OUT" "RESULT=CRASH exe=exit127 exit=127 signal=none" "$1"
    assert_absent "$OUT" "HOST-CLOSED" "$1: no HOST-CLOSED"
}

echo "host-closed: each identity field is required:"
set_events "$(event exit127.exe "$(native_path elsewhere/exit127)" 1234 "$HC_START_HEX" "$HC_TIME_CURRENT")"
assert_crash "same basename, different full path"
set_events "$(event exit127.exe "$P127" 1235 "$HC_START_HEX" "$HC_TIME_CURRENT")"
assert_crash "different pid"
set_events "$(event exit127.exe "$P127" 1234 01dd4d430767fcfc "$HC_TIME_CURRENT")"
assert_crash "different creation FILETIME"

echo "host-closed: the event time must sit around the observed exit:"
set_events "$(event exit127.exe "$P127" 1234 "$HC_START_HEX" 2026-09-25T12:00:05.5Z)"
assert_crash "exact identity, event before exit - pre-exit slack"
set_events "$(event exit127.exe "$P127" 1234 "$HC_START_HEX" 2026-09-25T12:00:12.5Z)"
assert_crash "exact identity, event after exit + post-exit slack"

echo "host-closed: same path + pid from another launch stays CRASH:"
set_events "$(event exit127.exe "$P127" 1234 01dd4d42e0ebf096 2026-09-25T11:59:00Z)"
assert_crash "stale event from an earlier launch"
set_events "$(event exit127.exe "$P127" 1234 01dd4d4400000000 2026-09-25T12:00:11Z)"
assert_crash "later launch reusing the pid, event inside the post-exit slack"

echo "host-closed: no captured identity stays CRASH:"
set_events "$MATCH_127"
IR_RUN_HOST_CLOSE_IDENTITY="$STUBS/no-such-identity" run_ir --timeout 10 exit127
assert_contains "$OUT" "RESULT=CRASH exe=exit127 exit=127" "identity lookup failure fails closed"

echo "host-closed: a late-published event is found by polling:"
set_events "$MATCH_127"
export HC_FIRST_ATTEMPT=2 IR_RUN_HOST_CLOSE_ATTEMPTS=3
run_ir --timeout 10 exit127
unset HC_FIRST_ATTEMPT
export IR_RUN_HOST_CLOSE_ATTEMPTS=1
assert_contains "$OUT" "RESULT=HOST-CLOSED exe=exit127 exit=127" "event withheld until attempt 2 is found"
mapfile -t QUERIES < "$HC_QUERY_LOG"
assert_eq "${#QUERIES[@]}" "2" "polling stops at the first match"
assert_eq "${QUERIES[0]#1 }" "${QUERIES[1]#2 }" "every attempt queries the same identity and bounds"
assert_eq "${QUERIES[0]#1 }" "$P127 $HC_NATIVE_PID $HC_FILETIME 1790337607.500000 1790337611.500000" \
    "bounds are [max(start, exit - 3), exit + 1]"
set_events ""

summarize "ir-run result-reporting tests"

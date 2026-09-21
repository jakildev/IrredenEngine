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
#
# Hermetic: a fake build dir with tiny scripts stands in for real demos.
# The plain --timeout cases take no ir-acquire lock; the lock-queue cases
# use an isolated IR_LOCK_ROOT so they never touch the host's real locks.

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

run_ir() {
    # Runs ir-run with the fake build dir; captures combined output and rc.
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

summarize "ir-run result-reporting tests"

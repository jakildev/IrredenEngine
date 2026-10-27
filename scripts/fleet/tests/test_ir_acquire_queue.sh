#!/usr/bin/env bash
# ir-acquire liveness while queued: CPU slot walks serialize on the cpu/walk
# gate, two full-budget benchmarks both get their slots, and a signal ends a
# queued requester or a no-command hold instead of resuming it.
#
# Every case runs on its own lock root. SIGINT is not exercised: a background
# job of a non-interactive shell starts with it ignored, and bash cannot trap
# a signal that was ignored on entry.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
HELPERS="$REPO_ROOT/engine/tools/lib/concurrency_helpers.sh"
ACQUIRE="$REPO_ROOT/engine/tools/bin/ir-acquire"

# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$ACQUIRE" ]]; then
    echo "SKIP: ir-acquire not found at $ACQUIRE" >&2
    exit 3
fi

TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/ir-acquire-queue.XXXXXX")
PIDS=()
# A child forked for `cmd &` and signalled before it execs runs this inherited
# trap; only the suite's own shell may kill the cases and remove TMP_ROOT. The
# child leaves through a bare `exit`, which keeps its signal status.
cleanup() {
    [[ "$BASHPID" == "$$" ]] || exit
    local p
    for p in ${PIDS[@]+"${PIDS[@]}"}; do kill -KILL "$p" 2>/dev/null || true; done
    for p in ${PIDS[@]+"${PIDS[@]}"}; do wait "$p" 2>/dev/null || true; done
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

unset IR_QUIET_OWNER IR_RUN_LOCK_TOKEN IR_ACQUIRE_HOLDER_PID IR_ACQUIRE_HELD_VERB
export FLEET_STATE_DIR="$TMP_ROOT/fleet-state"
export FLEET_CONF=/dev/null
export IR_QUIET_LINGER=0
export IR_QUIET_MAX=10
export IR_QUIET_DRAIN=1
export IR_QUIET_SETTLE_CPU=0.25
export IR_QUIET_SETTLE_SAMPLE=0.05

# case_root <name> — a fresh lock root, exported for the case.
case_root() {
    CASE="$TMP_ROOT/$1"
    mkdir -p "$CASE"
    export IR_LOCK_ROOT="$CASE/locks"
}

# seed_gate <pid> — stamps cpu/walk for <pid> through the lock primitive.
seed_gate() {
    bash -c 'source "$1"; _ir_try_lock "$IR_LOCK_ROOT/cpu/walk" "$2" ""' _ "$HELPERS" "$1"
}

# wait_bounded <pid> <secs> — reaps <pid> into WAIT_RC, SIGKILLing it first if
# it outlives <secs>; a requester that ignored its signal reads 137.
wait_bounded() {
    local pid="$1" secs="$2" dog
    ( sleep "$secs"; kill -KILL "$pid" 2>/dev/null ) </dev/null >/dev/null 2>&1 &
    dog=$!
    WAIT_RC=0
    wait "$pid" || WAIT_RC=$?
    kill "$dog" 2>/dev/null
    wait "$dog" 2>/dev/null
}

# with_default_signal <SIG> <cmd...> & — execs <cmd> with <SIG> at its default
# disposition, so `$!` is <cmd> itself; background calls only. A suite started
# with a signal ignored (fleet-jobs ignores HUP) passes that to every child,
# and bash cannot trap a signal ignored on entry.
with_default_signal() {
    exec python3 -c 'import os, signal, sys
signal.signal(getattr(signal, "SIG" + sys.argv[1]), signal.SIG_DFL)
os.execvp(sys.argv[2], sys.argv[2:])' "$@"
}

slot_owners() {
    local f
    for f in "$IR_LOCK_ROOT"/cpu/slot-*/owner; do
        [[ -f "$f" ]] && head -n 1 "$f"
    done
}

# wait_for <secs> <cmd...> — polls <cmd> every 0.05 s until it succeeds.
wait_for() {
    local secs="$1" i
    shift
    for (( i = 0; i < secs * 20; i++ )); do
        "$@" 2>/dev/null && return 0
        sleep 0.05
    done
    return 1
}

echo "== a held walk gate holds every CPU acquisition"
case_root gate
sleep 300 & gate_holder=$!
PIDS+=("$gate_holder")
seed_gate "$gate_holder"
assert_eq "$(head -n 1 "$IR_LOCK_ROOT/cpu/walk/owner" 2>/dev/null)" "$gate_holder" \
    "the gate is seeded for a live holder"
"$ACQUIRE" --timeout 10 cpu 1 -- touch "$CASE/M" 2>"$CASE/err" & requester=$!
PIDS+=("$requester")
sleep 1
assert_eq "$([[ -e "$CASE/M" ]] && echo ran || echo held)" "held" \
    "the command does not run while another process holds the gate"
assert_absent "$(slot_owners)" "$requester" "the requester owns no slot while the gate is held"
kill "$gate_holder" 2>/dev/null
wait "$gate_holder" 2>/dev/null
wait_bounded "$requester" 8
assert_eq "$WAIT_RC" "0" "the requester takes a dead holder's gate and runs"
assert_eq "$([[ -e "$CASE/M" ]] && echo ran || echo held)" "ran" "the command ran after the gate freed"
assert_eq "$([[ -e "$IR_LOCK_ROOT/cpu/walk/owner" ]] && echo held || echo free)" "free" \
    "the gate is released once the walk ends"

echo "== two full-budget benchmarks both get their slots"
case_root livelock
IR_CPU_BUDGET=14 "$ACQUIRE" --timeout 20 benchmark -- true >"$CASE/a.out" 2>&1 & a_pid=$!
IR_CPU_BUDGET=14 "$ACQUIRE" --timeout 20 benchmark -- true >"$CASE/b.out" 2>&1 & b_pid=$!
PIDS+=("$a_pid" "$b_pid")
wait_bounded "$a_pid" 30; a_rc=$WAIT_RC
wait_bounded "$b_pid" 30; b_rc=$WAIT_RC
assert_eq "$a_rc" "0" "the first concurrent 13-slot benchmark runs"
assert_eq "$b_rc" "0" "the second concurrent 13-slot benchmark runs"
assert_absent "$(cat "$CASE/a.out" "$CASE/b.out")" "timeout waiting" "neither benchmark times out on slots"

# signal_case <TERM|HUP> <expected-rc>
signal_case() {
    local sig="$1" want="$2" slots holder queued
    echo "== $sig ends a queued requester"
    case_root "signal-$sig"
    IR_CPU_BUDGET=2 "$ACQUIRE" cpu 2 -- sh -c "until [ -f '$CASE/R' ]; do sleep .05; done" & holder=$!
    PIDS+=("$holder")
    wait_for 5 test -f "$IR_LOCK_ROOT/cpu/slot-2/owner"
    slots="$(slot_owners | sort -u)"
    assert_eq "$slots" "$holder" "the holder owns both slots"
    IR_CPU_BUDGET=2 with_default_signal "$sig" "$ACQUIRE" --timeout 30 cpu 1 -- touch "$CASE/M" \
        2>/dev/null & queued=$!
    PIDS+=("$queued")
    sleep 0.5
    kill -"$sig" "$queued"
    wait_bounded "$queued" 2
    assert_eq "$WAIT_RC" "$want" "$sig ends the queued requester within 2 s"
    : > "$CASE/R"
    wait "$holder" 2>/dev/null
    sleep 1
    assert_eq "$([[ -e "$CASE/M" ]] && echo ran || echo absent)" "absent" \
        "the signalled requester never runs its command"
}
signal_case TERM 143
signal_case HUP 129

echo "== TERM ends a no-command hold"
case_root hold
"$ACQUIRE" gpu 2>"$CASE/err" & hold=$!
PIDS+=("$hold")
held=false
wait_for 5 grep -q 'held;' "$CASE/err" && held=true
assert_eq "$held" "true" "the hold prints its banner"
sleeper="$(pgrep -P "$hold" 2>/dev/null | head -n 1)"
kill -TERM "$hold"
wait_bounded "$hold" 2
assert_eq "$WAIT_RC" "143" "TERM ends the hold within 2 s"
assert_eq "$([[ -e "$IR_LOCK_ROOT/gpu/lock" ]] && echo held || echo free)" "free" \
    "the hold releases the gpu lock"
sleeper_left=false
[[ -z "$sleeper" ]] || wait_for 1 sh -c '! kill -0 "$1"' _ "$sleeper" || sleeper_left=true
assert_eq "$sleeper_left" "false" "the hold leaves no sleep child behind"

summarize "ir-acquire queue tests"

#!/usr/bin/env bash
# concurrency_test.sh — deterministic lock + budget tests for ir-acquire.
#
# Runs against the live ir-acquire binary in engine/tools/bin/. Uses an
# isolated lock dir so the test doesn't trample a real fleet run on the
# same host. No GPU needed — pure shell behavior.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IR_ACQUIRE="$REPO_ROOT/engine/tools/bin/ir-acquire"

# Isolate the test from any concurrent ir-* invocation on the box.
export XDG_RUNTIME_DIR
XDG_RUNTIME_DIR="$(mktemp -d)"
export IR_CPU_BUDGET=4   # small budget so contention is reachable

cleanup() {
    rm -rf "$XDG_RUNTIME_DIR"
}
trap cleanup EXIT

pass=0
fail=0
check() {
    local label="$1" expect="$2" actual="$3"
    if [[ "$expect" == "$actual" ]]; then
        echo "  PASS: $label"
        pass=$(( pass + 1 ))
    else
        echo "  FAIL: $label (expected '$expect', got '$actual')"
        fail=$(( fail + 1 ))
    fi
}

echo "[1] free budget reports correctly"
out="$("$IR_ACQUIRE" --info | awk '/cpu budget:/ {print $4}')"
check "0 slots in use at start" "(0" "$out"

echo "[2] cpu lock acquires + holds during wrapped command"
"$IR_ACQUIRE" cpu 2 -- bash -c '
    in_use="$('"$IR_ACQUIRE"' --info | awk "/cpu budget:/ {print \$4}")"
    if [[ "$in_use" == "(2" ]]; then exit 0; else exit 42; fi
'
check "2 cpu slots seen as in-use during wrap" "0" "$?"

echo "[3] cpu lock releases on EXIT"
in_use_after="$("$IR_ACQUIRE" --info | awk '/cpu budget:/ {print $4}')"
check "0 cpu slots after wrap exit" "(0" "$in_use_after"

echo "[4] cpu lock contention with --nonblock fails fast"
"$IR_ACQUIRE" cpu 3 -- bash -c '
    if '"$IR_ACQUIRE"' --nonblock cpu 2 -- true 2>/dev/null; then
        exit 1
    else
        exit 0
    fi
'
check "second 2-slot acquire blocked when only 1 free" "0" "$?"

echo "[5] gpu lock is exclusive"
"$IR_ACQUIRE" gpu -- bash -c '
    if '"$IR_ACQUIRE"' --nonblock gpu -- true 2>/dev/null; then
        exit 1
    else
        exit 0
    fi
'
check "second gpu acquire blocked under first" "0" "$?"

echo "[6] perf lock is exclusive"
"$IR_ACQUIRE" perf -- bash -c '
    if '"$IR_ACQUIRE"' --nonblock perf -- true 2>/dev/null; then
        exit 1
    else
        exit 0
    fi
'
check "second perf acquire blocked under first" "0" "$?"

echo "[7] benchmark mode acquires cpu(budget-1) + gpu + perf"
"$IR_ACQUIRE" benchmark -- bash -c '
    state="$('"$IR_ACQUIRE"' --info)"
    grep -q "cpu budget: 4 (3 in use, 1 free)" <<< "$state" || exit 11
    grep -q "gpu lock: held" <<< "$state" || exit 12
    grep -q "perf lock: held" <<< "$state" || exit 13
'
check "benchmark mode holds cpu(budget-1)+gpu+perf" "0" "$?"

echo "[8] PID-death recovery — kill a holder mid-flight, ensure stale-cleanup"
(
    "$IR_ACQUIRE" gpu -- bash -c 'sleep 30' &
    holder_pid=$!
    sleep 0.5
    # Kill the holder; the trap WON'T run because SIGKILL bypasses it.
    kill -KILL "$holder_pid" 2>/dev/null || true
    wait "$holder_pid" 2>/dev/null || true
    # The next acquire should detect the dead PID and reclaim.
    if "$IR_ACQUIRE" --nonblock gpu -- true 2>/dev/null; then
        exit 0
    else
        exit 1
    fi
)
check "reclaim after SIGKILL'd holder" "0" "$?"

HELPERS="$REPO_ROOT/engine/tools/lib/concurrency_helpers.sh"
IR_RUN="$REPO_ROOT/engine/tools/bin/ir-run"

echo "[9] wrapped command inherits the holder pid + verb"
out="$("$IR_ACQUIRE" gpu -- bash -c 'echo "$IR_ACQUIRE_HELD_VERB:${IR_ACQUIRE_HOLDER_PID:+set}"')"
check "IR_ACQUIRE_HELD_VERB/HOLDER_PID exported" "gpu:set" "$out"

echo "[10] benchmark hold covers gpu, perf, benchmark; gpu hold covers only gpu"
out="$("$IR_ACQUIRE" benchmark -- bash -c '
    source "'"$HELPERS"'"
    for v in gpu perf benchmark; do
        ir_inherited_lock_covers "$v" && printf "%s+ " "$v" || printf "%s- " "$v"
    done')"
check "under benchmark" "gpu+ perf+ benchmark+ " "$out"
out="$("$IR_ACQUIRE" gpu -- bash -c '
    source "'"$HELPERS"'"
    for v in gpu perf benchmark; do
        ir_inherited_lock_covers "$v" && printf "%s+ " "$v" || printf "%s- " "$v"
    done')"
check "under gpu" "gpu+ perf- benchmark- " "$out"

echo "[11] an env naming a pid that does not own the lock covers nothing"
out="$(IR_ACQUIRE_HOLDER_PID=1 IR_ACQUIRE_HELD_VERB=benchmark bash -c '
    source "'"$HELPERS"'"
    ir_inherited_lock_covers gpu && echo covered || echo not')"
check "stale env, free lock" "not" "$out"
out="$("$IR_ACQUIRE" gpu -- env IR_ACQUIRE_HOLDER_PID=1 bash -c '
    source "'"$HELPERS"'"
    ir_inherited_lock_covers gpu && echo covered || echo not')"
check "env pid is not the lock's holder" "not" "$out"
# The other Cygwin runtime's holder can carry the ancestor's pid number; only
# the winpid record tells them apart.
out="$("$IR_ACQUIRE" gpu -- bash -c '
    source "'"$HELPERS"'"
    echo "4242 C:/other-runtime" > "$IR_LOCK_ROOT/gpu/lock/winpid"
    ir_inherited_lock_covers gpu && echo covered || echo not')"
check "same pid, another runtime's winpid on the lock" "not" "$out"
out="$("$IR_ACQUIRE" gpu -- env IR_ACQUIRE_HOLDER_WINPID="4242 C:/other-runtime" bash -c '
    source "'"$HELPERS"'"
    ir_inherited_lock_covers gpu && echo covered || echo not')"
check "same pid, env winpid is not the lock's" "not" "$out"

echo "[12] ir-run --auto-profile nested in ir-acquire benchmark runs, not queues"
FAKE_BUILD="$(mktemp -d)"
printf '#!/usr/bin/env bash\nexit 0\n' > "$FAKE_BUILD/ir_fake_exe"
chmod +x "$FAKE_BUILD/ir_fake_exe"
status=0
IR_QUEUE_TIMEOUT=3 "$IR_ACQUIRE" benchmark -- \
    "$IR_RUN" --build-dir "$FAKE_BUILD" --timeout 30 ir_fake_exe --auto-profile 1 \
    > "$FAKE_BUILD/run.log" 2>&1 || status=$?
check "nested auto-profile run exits clean" "0" "$status"
grep -q "RESULT=CLEAN" "$FAKE_BUILD/run.log" && out=clean || out="$(tail -3 "$FAKE_BUILD/run.log")"
check "nested run reports RESULT=CLEAN" "clean" "$out"
rm -rf "$FAKE_BUILD"

echo "[13] a holder from the other Cygwin runtime is judged by its Windows pid"
if [[ -r /proc/$$/winpid ]]; then
    # A pid this runtime cannot see, stamped with a live Windows pid (ours).
    out="$(bash -c '
        source "'"$HELPERS"'"
        lock="$IR_LOCK_ROOT/gpu/lock"
        mkdir "$lock"; echo 999999 > "$lock/pid"
        echo "$_IR_SELF_WINPID C:/other-runtime" > "$lock/winpid"
        _ir_try_lock "$lock" && printf "taken " || printf "held "
        echo "$_IR_SELF_WINPID $_IR_RUNTIME_ROOT" > "$lock/winpid"
        _ir_try_lock "$lock" && printf "taken" || printf "held"
        rm -rf "$lock"')"
    check "foreign-runtime live holder kept; same-runtime dead holder reclaimed" "held taken" "$out"
else
    echo "  SKIP: no /proc/\$\$/winpid (not a Cygwin runtime)"
fi

echo "[14] two runtimes sharing a pid release only their own locks"
# One shell plays both runtimes: same pid, a different winpid record each.
# Odd numbers are never real Windows pids, so neither reads as live.
out="$(bash -c '
    source "'"$HELPERS"'"
    as_runtime() { _IR_SELF_WINPID="$1"; _IR_RUNTIME_ROOT="$2"; }
    gpu="$IR_LOCK_ROOT/gpu/lock"; perf="$IR_LOCK_ROOT/perf/lock"
    as_runtime 99999991 C:/rt-a; _ir_try_lock "$gpu";  _ir_record_held "$gpu"
    as_runtime 99999993 C:/rt-b; _ir_try_lock "$perf"; _ir_record_held "$perf"
    _ir_release_one "$gpu"; [[ -d "$gpu" ]] && printf "kept " || printf "stolen "
    ir_release_all
    [[ -d "$gpu" ]] && printf "kept " || printf "stolen "
    [[ -d "$perf" ]] && printf "held " || printf "released "
    as_runtime 99999991 C:/rt-a; ir_release_all
    [[ -d "$gpu" ]] && printf "held" || printf "released"')"
check "rt-b exit leaves rt-a's lock; each releases its own" "kept kept released released" "$out"

echo "[15] sweeping a dead holder's ledger spares a same-pid lock of another runtime"
out="$(bash -c '
    source "'"$HELPERS"'"
    gpu="$IR_LOCK_ROOT/gpu/lock"; ledger="$IR_LOCK_ROOT/.held/999999.99999991"
    mkdir -p "$ledger" "$gpu"
    echo "99999991 C:/rt-a" > "$ledger/.winpid"; : > "$ledger/gpu__lock"
    echo 999999 > "$gpu/pid"; echo "99999993 C:/rt-b" > "$gpu/winpid"
    ir_sweep_stale
    [[ -d "$gpu" ]] && printf "kept " || printf "stolen "
    [[ -d "$ledger" ]] && printf "ledger-left " || printf "ledger-swept "
    mkdir -p "$ledger" "$gpu"
    echo "99999991 C:/rt-a" > "$ledger/.winpid"; : > "$ledger/gpu__lock"
    echo 999999 > "$gpu/pid"; echo "99999991 C:/rt-a" > "$gpu/winpid"
    ir_sweep_stale
    [[ -d "$gpu" ]] && printf "kept" || printf "swept"')"
check "foreign lock kept, the dead holder's own swept" "kept ledger-swept swept" "$out"

echo
echo "concurrency_test.sh: $pass passed, $fail failed"
exit "$fail"

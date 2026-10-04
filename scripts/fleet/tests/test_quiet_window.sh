#!/usr/bin/env bash
# Engine benchmark quiet-window integration tests.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
source "$SCRIPT_DIR/lib_assert.sh"

TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/quiet-window.XXXXXX")
HELPERS="$REPO_ROOT/engine/tools/lib/concurrency_helpers.sh"
mkdir -p "$TMP_ROOT/default-runtime"
DEFAULT_ROOT_EXPORTED=false
if env -u IR_LOCK_ROOT XDG_RUNTIME_DIR="$TMP_ROOT/default-runtime" bash -c \
    'source "$1"; env | grep -qx "IR_LOCK_ROOT=$XDG_RUNTIME_DIR/irreden/locks"' _ "$HELPERS"; then
    DEFAULT_ROOT_EXPORTED=true
fi
assert_eq "$DEFAULT_ROOT_EXPORTED" "true" \
    "the default lock root is exported to quiet helper children"
export IR_LOCK_ROOT="$TMP_ROOT/locks"
export FLEET_STATE_DIR="$TMP_ROOT/fleet-state"
export IR_QUIET_LINGER=0
export IR_QUIET_MAX=10
export IR_QUIET_DRAIN=1
export IR_QUIET_SETTLE_CPU=0.25
export IR_QUIET_SETTLE_SAMPLE=0.05
export PATH="$REPO_ROOT/engine/tools/bin:$REPO_ROOT/scripts/fleet:$PATH"
IR_ACQUIRE="$REPO_ROOT/engine/tools/bin/ir-acquire"
HOLDER_PID=""
PARK_PID=""
BENCH_PID=""
BUILD_PID=""
HOG_MARKER="$TMP_ROOT/hog"
HOG_START="$TMP_ROOT/hog.start"
HOG_END="$TMP_ROOT/hog.end"

cleanup() {
    [[ -n "$PARK_PID" ]] && kill "$PARK_PID" 2>/dev/null || true
    [[ -n "$BENCH_PID" ]] && kill "$BENCH_PID" 2>/dev/null || true
    [[ -n "$BUILD_PID" ]] && kill "$BUILD_PID" 2>/dev/null || true
    [[ -n "$HOLDER_PID" ]] && kill "$HOLDER_PID" 2>/dev/null || true
    wait "$PARK_PID" 2>/dev/null || true
    wait "$BENCH_PID" 2>/dev/null || true
    wait "$BUILD_PID" 2>/dev/null || true
    wait "$HOLDER_PID" 2>/dev/null || true
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

start_holder() {
    rm -f "$HOG_MARKER" "$HOG_START" "$HOG_END"
    python3 - "$HOG_MARKER" "$HOG_START" "$HOG_END" <<'PY' &
import pathlib
import sys
import time

marker, started, ended = map(pathlib.Path, sys.argv[1:])
while True:
    if marker.exists():
        started.write_text(str(time.time()))
        value = 0
        while marker.exists():
            value = (value + 1) % 1000003
        ended.write_text(str(time.time()))
    time.sleep(0.01)
PY
    HOLDER_PID=$!
}

wait_for_file() {
    local path="$1"
    for _attempt in $(seq 1 100); do
        [[ -s "$path" ]] && return 0
        sleep 0.02
    done
    return 1
}

OUT=$("$IR_ACQUIRE" --quiet-status 2>&1)
RC=$?
assert_eq "$RC" "1" "an empty lock root is not gated"
assert_contains "$OUT" "off" "empty status says off"
mkdir -p "$IR_LOCK_ROOT/quiet/records/malformed"
printf '{"phase":"held"}\n' > "$IR_LOCK_ROOT/quiet/records/malformed/record.json"
OUT=$("$IR_ACQUIRE" --quiet-status 2>&1)
RC=$?
assert_eq "$RC" "1" "malformed record is ignored by status"
assert_contains "$OUT" "off" "malformed record cannot crash the evaluator"
rm -rf "$IR_LOCK_ROOT/quiet/records/malformed"

"$IR_ACQUIRE" --quiet-disable test
GATE_OUT=$(fleet-gate-status 2>&1)
assert_contains "$GATE_OUT" "Quiet window: disabled reason=test" \
    "fleet-gate-status human output shows the disabled switch"
GATE_STATE=$(fleet-gate-status --json | python3 -c 'import json,sys; print(json.load(sys.stdin)["quiet_window"]["state"])' | tr -d '\r')
assert_eq "$GATE_STATE" "disabled" "fleet-gate-status JSON embeds the switch state"
REPORT="$TMP_ROOT/unguarded.report"
OUT=$(IR_QUIET_REPORT_FILE="$REPORT" "$IR_ACQUIRE" benchmark -- true 2>&1)
RC=$?
assert_eq "$RC" "0" "disabled benchmark preserves command success"
assert_contains "$OUT" "QUIET-UNGUARDED" "disabled benchmark names the unguarded result"
assert_eq "$(head -1 "$REPORT")" "UNGUARDED" "disabled report is unguarded"
"$IR_ACQUIRE" --quiet-enable

start_holder
"$IR_ACQUIRE" --quiet-lease stand-in create "$HOLDER_PID"
touch "$HOG_MARKER"
wait_for_file "$HOG_START"
REPORT="$TMP_ROOT/guarded.report"
IR_QUIET_DRAIN=3 IR_QUIET_REPORT_FILE="$REPORT" "$IR_ACQUIRE" benchmark -- \
    python3 -c "import pathlib,time; pathlib.Path('$TMP_ROOT/guarded.start').write_text(str(time.time())); time.sleep(.2)" \
    >"$TMP_ROOT/guarded.out" 2>&1 &
BENCH_PID=$!
for _attempt in $(seq 1 40); do
    OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
    [[ "$OUT" == *'"state": "waiting"'* || "$OUT" == *'"state": "draining"'* ]] && break
    sleep 0.05
done
IR_QUIET_OWNER=stand-in fleet-quiet-wait &
PARK_PID=$!
sleep 0.2
assert_absent "$(find "$TMP_ROOT" -maxdepth 1 -type f -print)" "$TMP_ROOT/guarded.start" \
    "foreign CPU work holds the benchmark behind the barrier"
rm -f "$HOG_MARKER"
wait_for_file "$HOG_END"
for _attempt in $(seq 1 40); do
    OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
    [[ "$OUT" == *'"parked": 1'* ]] && break
    sleep 0.05
done
assert_contains "$OUT" '"parked": 1' "a quiet leased session acknowledges parking"
GATE_JSON=$(fleet-gate-status --json)
case "$GATE_JSON" in
    *'"state": "draining"'*|*'"state": "held"'*) GATE_LIVE=true ;;
    *) GATE_LIVE=false ;;
esac
assert_eq "$GATE_LIVE" "true" "fleet-gate-status JSON follows the live evaluator"

wait "$BENCH_PID"
RC=$?
BENCH_PID=""
OUT=$(cat "$TMP_ROOT/guarded.out")
assert_eq "$RC" "0" "benchmark runs after every lease is parked"
assert_eq "$(head -1 "$REPORT")" "GUARDED" "covered hold reports guarded"
ORDERED=$(python3 - "$HOG_END" "$TMP_ROOT/guarded.start" <<'PY'
import pathlib, sys
print(float(pathlib.Path(sys.argv[2]).read_text()) >= float(pathlib.Path(sys.argv[1]).read_text()))
PY
)
assert_eq "$ORDERED" "True" "measurement starts after the foreign hog ends"
wait "$PARK_PID" 2>/dev/null || true
PARK_PID=""
"$IR_ACQUIRE" --quiet-lease stand-in drop
kill "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""

# A quiet lease that becomes busy after the barrier invalidates the hold even
# when it settles again before command exit.
start_holder
"$IR_ACQUIRE" --quiet-lease contaminator create "$HOLDER_PID"
REPORT="$TMP_ROOT/breach.report"
IR_QUIET_DRAIN=3 IR_QUIET_REPORT_FILE="$REPORT" "$IR_ACQUIRE" benchmark -- \
    sh -c "touch '$TMP_ROOT/hold.started'; while [ ! -f '$TMP_ROOT/hold.release' ]; do sleep .05; done" \
    >"$TMP_ROOT/breach.out" 2>&1 &
BENCH_PID=$!
for _attempt in $(seq 1 100); do
    OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
    [[ "$OUT" == *'"state": "waiting"'* || "$OUT" == *'"state": "draining"'* ]] && break
    sleep 0.02
done
IR_QUIET_OWNER=contaminator fleet-quiet-wait &
PARK_PID=$!
wait_for_file "$TMP_ROOT/hold.started"
OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
assert_contains "$OUT" '"parked": 1' "lease is parked when the hold begins"
touch "$HOG_MARKER"
wait_for_file "$HOG_START"
for _attempt in $(seq 1 100); do
    OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
    [[ "$OUT" == *'"busy": 1'* ]] && break
    sleep 0.02
done
assert_contains "$OUT" '"busy": 1' "post-barrier hog makes the lease busy"
EVENT_COUNT=0
EVENT_SEEN=false
for _attempt in $(seq 1 100); do
    EVENT_COUNT=$(find "$IR_LOCK_ROOT/quiet/records" -path '*/events/*.json' -print | wc -l | tr -d ' ')
    if (( EVENT_COUNT > 0 )); then EVENT_SEEN=true; break; fi
    sleep 0.02
done
assert_eq "$EVENT_SEEN" "true" "post-barrier activity appends a breach event"
rm -f "$HOG_MARKER"
wait_for_file "$HOG_END"
touch "$TMP_ROOT/hold.release"
wait "$BENCH_PID"
RC=$?
BENCH_PID=""
wait "$PARK_PID" 2>/dev/null || true
PARK_PID=""
OUT=$(cat "$TMP_ROOT/breach.out")
assert_eq "$RC" "76" "post-barrier CPU activity invalidates the acquisition"
assert_eq "$(head -1 "$REPORT")" "BREACH" "contamination is machine-readable"
assert_contains "$OUT" "QUIET-BREACH contaminator busy" "breach names the active lease"
"$IR_ACQUIRE" --quiet-lease contaminator drop
kill "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""

# A requester's own lease is sampled too. A sibling hog outside ir-acquire's
# measurement subtree refuses, while work inside the command is excluded.
"$IR_ACQUIRE" --quiet-lease requester create "$BASHPID"
python3 -c 'x=0
while True: x=(x+1)%1000003' &
HOLDER_PID=$!
REPORT="$TMP_ROOT/self-refused.report"
OUT=$(IR_QUIET_DRAIN=3 IR_QUIET_SETTLE_CPU=.5 IR_QUIET_OWNER=requester IR_QUIET_REPORT_FILE="$REPORT" \
    bash -c 'export IR_QUIET_MEASURE_ROOT=$$; "$1" benchmark -- touch "$2"' \
    _ "$IR_ACQUIRE" "$TMP_ROOT/self-ran" 2>&1)
RC=$?
assert_eq "$RC" "75" "requester sibling work refuses its own benchmark"
assert_absent "$(find "$TMP_ROOT" -maxdepth 1 -type f -print)" "$TMP_ROOT/self-ran" \
    "refused requester command never runs"
kill "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""
"$IR_ACQUIRE" --quiet-lease requester drop
REPORT="$TMP_ROOT/self-guarded.report"
OUT=$(IR_QUIET_REPORT_FILE="$REPORT" bash -c '
    tag=requester-control
    "$1" --quiet-lease "$tag" create "$$"
    IR_QUIET_OWNER="$tag" IR_QUIET_DRAIN=5 IR_QUIET_SETTLE_CPU=1.5 \
        bash -c '\''export IR_QUIET_MEASURE_ROOT=$$; "$1" benchmark -- python3 -c "$2"'\'' \
        _ "$1" "$2"
    rc=$?
    "$1" --quiet-lease "$tag" drop
    exit "$rc"
' _ "$IR_ACQUIRE" 'import os,time
pids=[]
for _ in range(4):
    pid=os.fork()
    if pid == 0:
        end=time.time()+.7; x=0
        while time.time()<end: x=(x+1)%1000003
        os._exit(0)
    pids.append(pid)
for pid in pids: os.waitpid(pid, 0)' 2>&1)
RC=$?
assert_eq "$RC" "0" "work inside the measurement subtree remains guarded"
assert_eq "$(head -1 "$REPORT")" "GUARDED" "measurement-subtree control is guarded"

sleep 30 &
HOLDER_PID=$!
"$IR_ACQUIRE" --quiet-lease busy-session create "$HOLDER_PID"
REPORT="$TMP_ROOT/refused.report"
OUT=$(IR_QUIET_REPORT_FILE="$REPORT" "$IR_ACQUIRE" benchmark -- touch "$TMP_ROOT/ran" 2>&1)
RC=$?
assert_eq "$RC" "75" "an undrained lease refuses the acquisition"
assert_absent "$(find "$TMP_ROOT" -maxdepth 1 -type f -print)" "$TMP_ROOT/ran" \
    "a refused benchmark never runs its command"
assert_eq "$(head -1 "$REPORT")" "REFUSED" "refusal is machine-readable"
"$IR_ACQUIRE" --quiet-lease busy-session drop
kill "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""

# Non-benchmark verbs never publish a quiet request, and dead holders are
# reaped by the evaluator.
"$IR_ACQUIRE" cpu 1 -- true
"$IR_ACQUIRE" gpu -- true
"$IR_ACQUIRE" perf -- true
RECORD_COUNT=$(find "$IR_LOCK_ROOT/quiet/records" -name record.json -print | wc -l | tr -d ' ')
assert_eq "$RECORD_COUNT" "0" "cpu, gpu, and perf acquisitions write no quiet record"
"$IR_ACQUIRE" --quiet-lease dead-holder create 999999
OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
assert_absent "$OUT" 'dead-holder' "dead-holder leases are ignored and reaped"

sleep 30 &
HOLDER_PID=$!
"$IR_ACQUIRE" --quiet-lease killed-parker create "$HOLDER_PID"
mkdir -p "$IR_LOCK_ROOT/quiet/leases/killed-parker/parkers"
printf '{"pid":999999,"state":"parked","quiet_since":0,"covered_through":9999999999}\n' \
    > "$IR_LOCK_ROOT/quiet/leases/killed-parker/parkers/dead.json"
OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
assert_contains "$OUT" '"tag": "killed-parker"' "live lease remains visible after its parker dies"
assert_contains "$OUT" '"state": "busy"' "killed parker makes its lease busy"
"$IR_ACQUIRE" --quiet-lease killed-parker drop
kill "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""

sleep 30 &
HOLDER_PID=$!
"$IR_ACQUIRE" --quiet-lease delayed-drain create "$HOLDER_PID"
RECORD=$(python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" record-create \
    --pid "$$" --owner delayed --linger 0 --maximum .1 --drain .1 \
    --settle-cpu .25 --settle-sample .05)
mkdir -p "$IR_LOCK_ROOT/quiet/leases/delayed-drain/parkers"
printf '{"pid":%s,"state":"parked","quiet_since":0,"covered_through":9999999999}\n' \
    "$HOLDER_PID" > "$IR_LOCK_ROOT/quiet/leases/delayed-drain/parkers/$HOLDER_PID.json"
sleep .2
OUT=$(python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" barrier "$RECORD" 2>&1)
RC=$?
assert_eq "$RC" "0" "lock-queue time does not consume the drain deadline"
REPORT="$TMP_ROOT/delayed-drain.report"
python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" finish "$RECORD" 0 --report "$REPORT"
RC=$?
assert_eq "$RC" "0" "lock-queue time does not consume the hold cap"
assert_eq "$(head -1 "$REPORT")" "GUARDED" "post-queue hold gets its full cap"
"$IR_ACQUIRE" --quiet-lease delayed-drain drop
kill "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""

TEST_PID="$BASHPID"
RECORD=$(IR_QUIET_NOW=100 python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" \
    record-create --pid "$TEST_PID" --owner edge --linger 0 --maximum 1 \
    --drain 1 --settle-cpu .25 --settle-sample .05)
OUT=$(IR_QUIET_NOW=102 "$IR_ACQUIRE" --quiet-status --json 2>&1)
assert_contains "$OUT" '"state": "expired"' "past the stamped cap the window is expired"
python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" refuse "$RECORD"

sleep 30 &
HOLDER_PID=$!
RECORD=$(python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" record-create \
    --pid "$HOLDER_PID" --owner doomed --linger 0 --maximum 10 --drain 1 \
    --settle-cpu .25 --settle-sample .05)
kill -9 "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""
OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
assert_contains "$OUT" '"state": "off"' "a killed requester is reaped to off"

IR_QUIET_LINGER=.5 "$IR_ACQUIRE" benchmark -- true
OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
assert_contains "$OUT" '"state": "linger"' "released request holds the stamped linger edge"
sleep .6
OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
assert_contains "$OUT" '"state": "off"' "gate opens after linger expires"

REPORT="$TMP_ROOT/cap.report"
OUT=$(IR_QUIET_MAX=.3 IR_QUIET_REPORT_FILE="$REPORT" \
    "$IR_ACQUIRE" benchmark -- sleep .5 2>&1)
RC=$?
assert_eq "$RC" "76" "a hold that crosses the cap is contaminated"
assert_contains "$(cat "$REPORT")" "cap" "cap breach is reported"

# A cooperating queued build parks instead of taking the last free CPU slot.
# Quiet-window deferral is not charged to its own short queue timeout.
sleep 30 &
HOLDER_PID=$!
"$IR_ACQUIRE" --quiet-lease queued-build create "$HOLDER_PID"
IR_QUIET_DRAIN=3 "$IR_ACQUIRE" benchmark -- \
    sh -c "touch '$TMP_ROOT/queue-hold.started'; while [ ! -f '$TMP_ROOT/queue-hold.release' ]; do sleep .05; done" \
    >"$TMP_ROOT/queue-benchmark.out" 2>&1 &
BENCH_PID=$!
for _attempt in $(seq 1 100); do
    OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
    [[ "$OUT" == *'"state": "waiting"'* || "$OUT" == *'"state": "draining"'* ]] && break
    sleep 0.02
done
IR_QUIET_OWNER=queued-build "$IR_ACQUIRE" --timeout 1 cpu 1 -- \
    touch "$TMP_ROOT/queued-build.started" >"$TMP_ROOT/queued-build.out" 2>&1 &
BUILD_PID=$!
wait_for_file "$TMP_ROOT/queue-hold.started"
sleep 1.2
assert_absent "$(find "$TMP_ROOT" -maxdepth 1 -type f -print)" "$TMP_ROOT/queued-build.started" \
    "queued build does not launch inside the held window"
touch "$TMP_ROOT/queue-hold.release"
wait "$BENCH_PID"
BENCH_RC=$?
BENCH_PID=""
wait "$BUILD_PID"
BUILD_RC=$?
BUILD_PID=""
assert_eq "$BENCH_RC" "0" "queued build parks without contaminating the benchmark"
assert_eq "$BUILD_RC" "0" "queued build grants after deferral exceeded its timeout"
assert_contains "$(find "$TMP_ROOT" -maxdepth 1 -type f -print)" "$TMP_ROOT/queued-build.started" \
    "queued build launches after the window closes"
"$IR_ACQUIRE" --quiet-lease queued-build drop
kill "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""

sleep 30 & holder_a=$!
sleep 30 & holder_b=$!
"$IR_ACQUIRE" --quiet-lease requester-a create "$holder_a"
"$IR_ACQUIRE" --quiet-lease requester-b create "$holder_b"
(
    IR_QUIET_OWNER=requester-a IR_QUIET_DRAIN=2 "$IR_ACQUIRE" benchmark -- \
        python3 -c "import pathlib,time; pathlib.Path('$TMP_ROOT/a.start').write_text(str(time.time())); time.sleep(.3); pathlib.Path('$TMP_ROOT/a.end').write_text(str(time.time()))"
    a_rc=$?
    IR_QUIET_OWNER=requester-a fleet-quiet-wait
    exit "$a_rc"
) >"$TMP_ROOT/a.out" 2>&1 & a_pid=$!
for _attempt in $(seq 1 100); do
    OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
    [[ "$OUT" == *'"state": "draining"'* || "$OUT" == *'"state": "held"'* ]] && break
    sleep .02
done
IR_QUIET_OWNER=requester-b IR_QUIET_DRAIN=2 "$IR_ACQUIRE" benchmark -- \
    python3 -c "import pathlib,time; pathlib.Path('$TMP_ROOT/b.start').write_text(str(time.time())); pathlib.Path('$TMP_ROOT/b.end').write_text(str(time.time()))" \
    >"$TMP_ROOT/b.out" 2>&1 & b_pid=$!
wait "$b_pid"; b_rc=$?
wait "$a_pid"; a_rc=$?
assert_eq "$a_rc" "0" "first competing benchmark exits guarded"
assert_eq "$b_rc" "0" "queued competing benchmark exits guarded"
ORDERED=$(python3 - "$TMP_ROOT/a.end" "$TMP_ROOT/b.start" <<'PY'
import pathlib, sys
print(float(pathlib.Path(sys.argv[2]).read_text()) >= float(pathlib.Path(sys.argv[1]).read_text()))
PY
)
assert_eq "$ORDERED" "True" "second benchmark starts after the first ends"
"$IR_ACQUIRE" --quiet-lease requester-a drop
"$IR_ACQUIRE" --quiet-lease requester-b drop
kill "$holder_a" "$holder_b" 2>/dev/null || true
wait "$holder_a" 2>/dev/null || true
wait "$holder_b" 2>/dev/null || true

sleep 30 & holder_a=$!
sleep 30 & holder_b=$!
"$IR_ACQUIRE" --quiet-lease no-park-a create "$holder_a"
"$IR_ACQUIRE" --quiet-lease no-park-b create "$holder_b"
IR_QUIET_OWNER=no-park-a IR_QUIET_DRAIN=.4 "$IR_ACQUIRE" benchmark -- true \
    >"$TMP_ROOT/no-park-a.out" 2>&1 & a_pid=$!
sleep .1
IR_QUIET_TEST_NO_QUEUE_PARK=1 IR_QUIET_OWNER=no-park-b IR_QUIET_DRAIN=.4 \
    "$IR_ACQUIRE" benchmark -- true >"$TMP_ROOT/no-park-b.out" 2>&1 & b_pid=$!
wait "$a_pid"; a_rc=$?
wait "$b_pid"; b_rc=$?
assert_eq "$a_rc" "75" "competing requester without queue parking is bounded"
assert_contains "$(cat "$TMP_ROOT/no-park-a.out")" "QUIET-REFUSED no-park-b" \
    "no-queue-park control names the competing requester"
assert_eq "$b_rc" "75" "the no-queue-park competitor also exits without deadlock"
"$IR_ACQUIRE" --quiet-lease no-park-a drop
"$IR_ACQUIRE" --quiet-lease no-park-b drop
kill "$holder_a" "$holder_b" 2>/dev/null || true
wait "$holder_a" 2>/dev/null || true
wait "$holder_b" 2>/dev/null || true

REPORT="$TMP_ROOT/mid-disable.report"
IR_QUIET_REPORT_FILE="$REPORT" "$IR_ACQUIRE" benchmark -- \
    sh -c "touch '$TMP_ROOT/mid-disable.started'; while [ ! -f '$TMP_ROOT/mid-disable.release' ]; do sleep .05; done" \
    >"$TMP_ROOT/mid-disable.out" 2>&1 &
BENCH_PID=$!
wait_for_file "$TMP_ROOT/mid-disable.started"
"$IR_ACQUIRE" --quiet-disable mid-hold
OUT=$("$IR_ACQUIRE" --quiet-status --json 2>&1)
RC=$?
assert_eq "$RC" "1" "disabled evaluator is an open gate"
assert_contains "$OUT" '"state": "disabled"' "all readers see a mid-hold switch flip"
touch "$TMP_ROOT/mid-disable.release"
wait "$BENCH_PID"
RC=$?
BENCH_PID=""
assert_eq "$RC" "76" "disabling during a guarded hold contaminates it"
assert_contains "$(cat "$REPORT")" "disabled" "mid-hold switch reason is reported"
"$IR_ACQUIRE" --quiet-enable

sleep 30 &
HOLDER_PID=$!
"$IR_ACQUIRE" --quiet-lease reenabled-session create "$HOLDER_PID"
OUT=$(IR_QUIET_DRAIN=.2 "$IR_ACQUIRE" benchmark -- true 2>&1)
RC=$?
assert_eq "$RC" "75" "re-enabled quiet window refuses a busy session again"
assert_contains "$OUT" "QUIET-REFUSED reenabled-session" \
    "re-enabled refusal names the busy lease"
"$IR_ACQUIRE" --quiet-lease reenabled-session drop
kill "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""

summarize "quiet-window tests"

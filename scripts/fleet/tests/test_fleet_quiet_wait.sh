#!/usr/bin/env bash
# Fast-path and release behavior for fleet-quiet-wait.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
source "$SCRIPT_DIR/lib_assert.sh"

TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-quiet-wait.XXXXXX")
HOLDER_PID=""
BENCH_PID=""
HOOK_PID=""
cleanup() {
    [[ -n "$HOOK_PID" ]] && kill "$HOOK_PID" 2>/dev/null || true
    [[ -n "$BENCH_PID" ]] && kill "$BENCH_PID" 2>/dev/null || true
    [[ -n "$HOLDER_PID" ]] && kill "$HOLDER_PID" 2>/dev/null || true
    wait "$HOOK_PID" 2>/dev/null || true
    wait "$BENCH_PID" 2>/dev/null || true
    wait "$HOLDER_PID" 2>/dev/null || true
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT
export IR_LOCK_ROOT="$TMP_ROOT/locks"
export IR_QUIET_LINGER=0 IR_QUIET_MAX=10 IR_QUIET_DRAIN=3
export IR_QUIET_SETTLE_CPU=.25 IR_QUIET_SETTLE_SAMPLE=.05
export PATH="$REPO_ROOT/engine/tools/bin:$REPO_ROOT/scripts/fleet:$PATH"

assert_quick_wait() {
    local label="$1" limit_ms="$2" started finished elapsed_ms out rc
    started=$(python3 -c 'import time; print(time.monotonic_ns())')
    out=$(fleet-quiet-wait 2>&1)
    rc=$?
    finished=$(python3 -c 'import time; print(time.monotonic_ns())')
    elapsed_ms=$(( (finished - started) / 1000000 ))
    assert_eq "$rc" "0" "$label returns successfully"
    assert_eq "$out" "" "$label writes no output"
    if (( elapsed_ms < limit_ms )); then
        ok "$label stays below ${limit_ms} ms (${elapsed_ms} ms)"
    else
        bad "$label stays below ${limit_ms} ms (${elapsed_ms} ms)"
    fi
}

assert_quick_wait "unset-owner fast path" 100

export IR_QUIET_OWNER=missing-lease
mkdir -p "$IR_LOCK_ROOT/quiet/records/fake"
printf '{"phase":"held"}\n' > "$IR_LOCK_ROOT/quiet/records/fake/record.json"
OUT=$(fleet-quiet-wait 2>&1)
RC=$?
assert_eq "$RC" "0" "missing lease fails open"
assert_eq "$OUT" "" "missing lease writes no output"
rm -rf "$IR_LOCK_ROOT/quiet/records/fake"

sleep 30 &
HOLDER_PID=$!
export IR_QUIET_OWNER=live-lease
ir-acquire --quiet-lease "$IR_QUIET_OWNER" create "$HOLDER_PID"
HOOK_COMMAND=$(python3 -c "import sys; sys.path.insert(0, '$REPO_ROOT/scripts/fleet'); import fleet_codex; print(fleet_codex.quiet_hook_command())")
IR_QUIET_REPORT_FILE="$TMP_ROOT/report" ir-acquire benchmark -- \
    sh -c "touch '$TMP_ROOT/command.started'; while [ ! -f '$TMP_ROOT/command.release' ]; do sleep .05; done" \
    >"$TMP_ROOT/benchmark.out" 2>&1 &
BENCH_PID=$!
for _attempt in $(seq 1 100); do
    STATUS=$(ir-acquire --quiet-status --json 2>&1)
    [[ "$STATUS" == *'"state": "waiting"'* || "$STATUS" == *'"state": "draining"'* ]] && break
    sleep 0.02
done
env -i HOME="$HOME" sh -c "$HOOK_COMMAND" >"$TMP_ROOT/hook.out" 2>&1 &
HOOK_PID=$!
for _attempt in $(seq 1 100); do
    STATUS=$(ir-acquire --quiet-status --json 2>&1)
    [[ "$STATUS" == *'"parked": 1'* ]] && break
    sleep 0.02
done
assert_contains "$STATUS" '"parked": 1' "live foreign window parks the leased session"
if kill -0 "$HOOK_PID" 2>/dev/null; then
    ok "hook remains blocked while the foreign window is held"
else
    bad "hook remains blocked while the foreign window is held"
fi
for _attempt in $(seq 1 100); do
    [[ -f "$TMP_ROOT/command.started" ]] && break
    sleep 0.02
done
touch "$TMP_ROOT/command.release"
wait "$BENCH_PID"
RC=$?
BENCH_PID=""
wait "$HOOK_PID"
HOOK_RC=$?
HOOK_PID=""
assert_eq "$RC" "0" "benchmark with a parked hook is guarded"
assert_eq "$HOOK_RC" "0" "hook exits successfully after release"
assert_eq "$(cat "$TMP_ROOT/hook.out")" "" "live hook keeps stdout empty"
STATUS=$(ir-acquire --quiet-status --json 2>&1)
assert_contains "$STATUS" '"busy": 1' "lease becomes busy before the hook returns"

requester_pid="$BASHPID"
record_one=$(python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" record-create \
    --pid "$requester_pid" --owner foreign-one --linger 0 --maximum 10 \
    --drain 3 --settle-cpu .25 --settle-sample .05)
IR_QUIET_TEST_UNPARK_HOLD="$TMP_ROOT/unpark-hold" \
    fleet-quiet-wait >"$TMP_ROOT/race-hook.out" 2>&1 &
HOOK_PID=$!
for _attempt in $(seq 1 100); do
    STATUS=$(ir-acquire --quiet-status --json 2>&1)
    [[ "$STATUS" == *'"parked": 1'* ]] && break
    sleep 0.02
done
python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" refuse "$record_one"
for _attempt in $(seq 1 100); do
    [[ -f "$TMP_ROOT/unpark-hold" ]] && break
    sleep 0.01
done
STATUS=$(ir-acquire --quiet-status --json 2>&1)
assert_contains "$STATUS" '"busy": 1' "hook writes busy before checking for a new window"
record_two=$(python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" record-create \
    --pid "$requester_pid" --owner foreign-two --linger 0 --maximum 10 \
    --drain 3 --settle-cpu .25 --settle-sample .05)
touch "$TMP_ROOT/unpark-hold.release"
for _attempt in $(seq 1 100); do
    STATUS=$(ir-acquire --quiet-status --json 2>&1)
    [[ "$STATUS" == *'"parked": 1'* ]] && break
    sleep 0.02
done
assert_contains "$STATUS" '"parked": 1' "unpark race re-parks under the new record"
if kill -0 "$HOOK_PID" 2>/dev/null; then
    ok "unpark race does not let the hook return through a new window"
else
    bad "unpark race does not let the hook return through a new window"
fi
python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" refuse "$record_two"
wait "$HOOK_PID"
HOOK_PID=""
assert_eq "$(cat "$TMP_ROOT/race-hook.out")" "" "unpark-race hook keeps stdout empty"

record_expired=$(IR_QUIET_NOW=100 python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" \
    record-create --pid "$BASHPID" --owner expired-owner --linger 0 --maximum 1 \
    --drain 1 --settle-cpu .25 --settle-sample .05)
export IR_QUIET_NOW=102
assert_quick_wait "expired hook" 500
unset IR_QUIET_NOW
python3 "$REPO_ROOT/engine/tools/lib/quiet_window.py" refuse "$record_expired"

ir-acquire --quiet-disable hook-test
assert_quick_wait "disabled hook" 500
ir-acquire --quiet-enable
ir-acquire --quiet-lease "$IR_QUIET_OWNER" drop
kill "$HOLDER_PID" 2>/dev/null || true
wait "$HOLDER_PID" 2>/dev/null || true
HOLDER_PID=""

summarize "fleet-quiet-wait tests"

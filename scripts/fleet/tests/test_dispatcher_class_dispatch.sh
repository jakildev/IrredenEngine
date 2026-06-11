#!/usr/bin/env bash
# Tests for fleet-dispatcher's per-task model-class resolution
# (--resolve-class, backed by resolve_worker_class + fleet_task_class.py).
#
# Covers:
#   - fable task resolves to the fable model at xhigh
#   - fable cap reached -> lane serves the next non-fable task (more=1)
#   - only cap-blocked fable work -> defer (keep trigger, no dispatch)
#   - per-task Effort: override threads through to the dispatch
#   - feedback severity routing (nits-only -> sonnet beats queued tasks)
#   - empty slice -> lane-default fallthrough (class empty)
#   - non-worker role is a no-op (class empty)
#
# The fable in-flight count comes from dispatch records under
# $FLEET_STATE_DIR/dispatch, same records --count-active reads.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"

if [[ ! -x "$DISPATCHER" ]]; then
    echo "test setup: fleet-dispatcher not found at $DISPATCHER" >&2
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
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"
mkdir -p "$FLEET_STATE_DIR/projections" "$FLEET_STATE_DIR/dispatch"
touch "$FLEET_CONF"

# Pin the class table so assertions don't depend on host env.
export FLEET_MODEL_FABLE='claude-fable-5[1m]'
export FLEET_MODEL_OPUS='claude-opus-4-8[1m]'
export FLEET_MODEL_SONNET='sonnet'
export FLEET_CONCURRENCY_MODEL_FABLE=1

write_slice() {
    # $1 = role, $2 = JSON body
    printf '%s\n' "$2" > "$FLEET_STATE_DIR/projections/$1.json"
}

resolve() {
    "$DISPATCHER" --resolve-class "$1"
}

# --- T1: fable task -> fable model at xhigh ---------------------------------
echo "T1: fable task resolves to fable model"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"fable","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(resolve worker)" \
    "class=fable model=claude-fable-5[1m] effort=xhigh more=0 defer=0" \
    "uncapped fable task dispatches on fable at xhigh"

# --- T2: fable cap reached -> next non-fable task ---------------------------
echo "T2: fable cap diverts to the next class"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"fable","effort":null,"owner":"free","blocked":false},{"issue":"#11","model":"opus","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
printf '{"role":"worker","pane":"%%9","class":"fable","dispatched_at":"x"}\n' \
    > "$FLEET_STATE_DIR/dispatch/pane-9.json"
assert_eq "$(resolve worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=xhigh more=0 defer=0" \
    "capped fable skipped; opus task served; cap-blocked fable does NOT hold the trigger (more=0)"

# --- T3: only capped fable work -> defer -------------------------------------
echo "T3: only cap-blocked fable work defers"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"fable","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
out=$(resolve worker)
assert_eq "$out" "class= model= effort= more=0 defer=1" \
    "cap-blocked fable-only slice -> defer (no lane-default burn)"
rm -f "$FLEET_STATE_DIR/dispatch/pane-9.json"

# --- T4: per-task effort override --------------------------------------------
echo "T4: Effort: override threads through"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"opus","effort":"medium","owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(resolve worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=medium more=0 defer=0" \
    "task-level Effort: medium beats the class default"

# --- T5: feedback severity routing -------------------------------------------
echo "T5: nits-only feedback routes sonnet ahead of queued tasks"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"opus","effort":null,"owner":"free","blocked":false}],"feedback_prs":[{"number":50,"labels":["fleet:approved","fleet:has-nits"]}],"needs_plan":[]}'
assert_eq "$(resolve worker)" \
    "class=sonnet model=sonnet effort=high more=1 defer=0" \
    "has-nits feedback dispatches sonnet; opus task keeps the trigger"

# --- T6: empty slice -> lane default fallthrough ------------------------------
echo "T6: empty slice falls through to lane default"
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(resolve worker)" "class= model= effort= more=0 defer=0" \
    "empty slice -> lane-default dispatch (reservation-resume path)"

# --- T7: non-worker role is a no-op ------------------------------------------
echo "T7: non-worker roles skip class resolution"
assert_eq "$(resolve merger)" "class= model= effort= more=0 defer=0" \
    "merger has no lane class; resolution is a no-op"

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]

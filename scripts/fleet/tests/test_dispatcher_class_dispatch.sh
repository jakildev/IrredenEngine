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
#   - planning pre-claim (#2197): plan=1 election, --plan-assign claim walk
#     (grant / held-fallthrough / exit-3 --replan / all-held / game --repo
#     namespacing / dry-run+review-only gating) against a stubbed fleet-claim
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
    "class=fable model=claude-fable-5[1m] effort=xhigh more=0 defer=0 count=1 plan=0" \
    "uncapped fable task dispatches on fable at xhigh"

# --- T2: fable cap reached -> next non-fable task ---------------------------
echo "T2: fable cap diverts to the next class"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"fable","effort":null,"owner":"free","blocked":false},{"issue":"#11","model":"opus","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
printf '{"role":"worker","pane":"%%9","class":"fable","dispatched_at":"x"}\n' \
    > "$FLEET_STATE_DIR/dispatch/pane-9.json"
assert_eq "$(resolve worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=xhigh more=0 defer=0 count=1 plan=0" \
    "capped fable skipped; opus task served; cap-blocked fable does NOT hold the trigger (more=0)"

# --- T3: only capped fable work -> defer -------------------------------------
echo "T3: only cap-blocked fable work defers"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"fable","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
out=$(resolve worker)
assert_eq "$out" "class= model= effort= more=0 defer=1 count= plan=0" \
    "cap-blocked fable-only slice -> defer (no lane-default burn)"
rm -f "$FLEET_STATE_DIR/dispatch/pane-9.json"

# --- T4: per-task effort override --------------------------------------------
echo "T4: Effort: override threads through"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"opus","effort":"medium","owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(resolve worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=medium more=0 defer=0 count=1 plan=0" \
    "task-level Effort: medium beats the class default"

# --- T5: feedback severity routing -------------------------------------------
echo "T5: nits-only feedback routes sonnet ahead of queued tasks"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"opus","effort":null,"owner":"free","blocked":false}],"feedback_prs":[{"number":50,"labels":["fleet:approved","fleet:has-nits"]}],"needs_plan":[]}'
assert_eq "$(resolve worker)" \
    "class=sonnet model=sonnet effort=high more=1 defer=0 count=1 plan=0" \
    "has-nits feedback dispatches sonnet; opus task keeps the trigger"

# --- T6: empty slice -> lane default fallthrough ------------------------------
echo "T6: empty slice falls through to lane default"
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(resolve worker)" "class= model= effort= more=0 defer=0 count= plan=0" \
    "empty slice -> lane-default dispatch (reservation-resume path)"

# --- T7: non-worker role is a no-op ------------------------------------------
echo "T7: non-worker roles skip class resolution"
assert_eq "$(resolve merger)" "class= model= effort= more=0 defer=0 count= plan=0" \
    "merger has no lane class; resolution is a no-op"

# --- T8: cross-class exclude threads through resolve_worker_class -------------
# The dispatcher's cross-class fan-out re-resolves excluding a cap-covered class.
echo "T8: --resolve-class <role> <exclude> serves the next class"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"opus","effort":null,"owner":"free","blocked":false},{"issue":"#11","model":"sonnet","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$("$DISPATCHER" --resolve-class worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=xhigh more=1 defer=0 count=1 plan=0" \
    "no exclude -> opus elected, sonnet is 'more'"
assert_eq "$("$DISPATCHER" --resolve-class worker opus)" \
    "class=sonnet model=sonnet effort=high more=0 defer=0 count=1 plan=0" \
    "exclude opus -> sonnet served (the cross-class fan-out)"
assert_eq "$("$DISPATCHER" --resolve-class worker opus,sonnet)" \
    "class= model= effort= more=0 defer=1 count= plan=0" \
    "exclude both claimable classes -> defer (not lane-default)"

# --- T9+: planning pre-claim (#2197) ------------------------------------------
# The dispatcher takes the planning-claim label lock itself (under the target
# pane's worktree basename) BEFORE dispatching, and hands the assignment to the
# dispatch. Exercised via the --plan-assign hook, which runs the same
# resolve + plan_assign_for_pane path a live tick does. fleet-claim is stubbed
# (hermetic — scripts/fleet/CLAUDE.md): grant/held/planned sets come from env,
# every invocation is logged for argv assertions.
export FLEET_CLAIM_LOG="$TMPROOT/fleet-claim.log"
STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/fleet-claim" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$FLEET_CLAIM_LOG"
repo="engine"
if [[ "${1:-}" == "--repo" ]]; then repo="$2"; shift 2; fi
sub="${1:-}"; num="${2:-}"
replan=""
[[ "${4:-}" == "--replan" ]] && replan=1
key="$repo:$num"
[[ "$sub" == "planning-release" ]] && exit 0
if [[ -n "$replan" ]]; then
    [[ " ${STUB_REPLAN_GRANT:-} " == *" $key "* ]] && exit 0
    exit 2
fi
[[ " ${STUB_GRANT:-} " == *" $key "* ]] && exit 0
[[ " ${STUB_PLANNED:-} " == *" $key "* ]] && exit 3
exit 1
EOF
chmod +x "$STUB_BIN/fleet-claim"
export PATH="$STUB_BIN:$PATH"

plan_assign() {
    : > "$FLEET_CLAIM_LOG"
    "$DISPATCHER" --plan-assign worker worker-9
}

echo "T9: needs-plan slice resolves plan=1 on the elected class"
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[{"number":99,"repo":"engine","labels":[]},{"number":120,"repo":"engine","labels":[]},{"number":7,"repo":"game","labels":[]}]}'
assert_eq "$(resolve worker)" \
    "class=fable model=claude-fable-5[1m] effort=xhigh more=0 defer=0 count=1 plan=1" \
    "untagged needs-plan elects fable with plan=1"

echo "T10: assignment granted on the first candidate, claimed under the agent"
assert_eq "$(STUB_GRANT='engine:99' plan_assign)" "plan=engine:99" \
    "first candidate claim granted -> assigned"
grep -q '^planning-claim 99 worker-9$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: claim ran under the pane worktree basename"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: claim argv wrong: $(cat "$FLEET_CLAIM_LOG")"; }

echo "T11: held candidate falls through to the next line (lost race != burned dispatch)"
assert_eq "$(STUB_GRANT='engine:120' plan_assign)" "plan=engine:120" \
    "engine:99 held elsewhere (exit 1) -> engine:120 assigned"

echo "T12: exit-3 + live needs-plan retries with --replan and assigns"
assert_eq "$(STUB_PLANNED='engine:99' STUB_REPLAN_GRANT='engine:99' plan_assign)" "plan=engine:99" \
    "stale-slice/plan-review re-plan state -> assigned via --replan"
grep -q '^planning-claim 99 worker-9 --replan$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: --replan retry issued"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: no --replan retry: $(cat "$FLEET_CLAIM_LOG")"; }

echo "T13: exit-3 then replan exit-2 (genuinely done) -> next line"
assert_eq "$(STUB_PLANNED='engine:99' STUB_GRANT='engine:120' plan_assign)" "plan=engine:120" \
    "already-planned candidate skipped; next line assigned"

echo "T14: all candidates held/planned -> no assignment"
assert_eq "$(plan_assign)" "plan=" \
    "every claim refused -> plan= (dispatch_role shrinks the headroom)"

echo "T15: game-repo candidate claims with --repo game before the subcommand"
assert_eq "$(STUB_GRANT='game:7' plan_assign)" "plan=game:7" \
    "engine lines held -> game line assigned"
grep -q -- '^--repo game planning-claim 7 worker-9$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: game claim namespaced with --repo game"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: game claim argv wrong: $(cat "$FLEET_CLAIM_LOG")"; }

echo "T16: dry-run / review-only never pre-claim"
printf 'dry-run\n' > "$FLEET_STATE_DIR/dispatch-mode"
assert_eq "$(STUB_GRANT='engine:99' plan_assign)" "plan=" \
    "dry-run mode -> no assignment"
[[ ! -s "$FLEET_CLAIM_LOG" ]] \
    && { PASS=$((PASS+1)); echo "  ok: no fleet-claim call in dry-run"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: dry-run still called fleet-claim: $(cat "$FLEET_CLAIM_LOG")"; }
printf 'review-only\n' > "$FLEET_STATE_DIR/dispatch-mode"
assert_eq "$(STUB_GRANT='engine:99' plan_assign)" "plan=" \
    "review-only mode -> no assignment"
rm -f "$FLEET_STATE_DIR/dispatch-mode"

echo "T17: a plan-carrying dispatch command appends the 7th plan= arg"
# build_dispatch_command is exercised via --print-dispatch-command for the
# 6-arg (no assignment) shape; the 7-arg shape is asserted through the log of
# a live-shaped assignment (T10) + the wrap-side export test
# (test_dispatch_wrap_session.sh). Here: no assignment -> 6 args, no plan=.
out=$("$DISPATCHER" --print-dispatch-command worker pane-3)
case "$out" in
    *" plan="*) FAIL=$((FAIL+1)); echo "  FAIL: unassigned dispatch carries plan=: $out" ;;
    *) PASS=$((PASS+1)); echo "  ok: unassigned dispatch has no plan= arg" ;;
esac

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]

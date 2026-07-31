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
#   - semantic-conflict-only slice -> opus dispatch (step-1c pressure, #2417)
#   - empty slice -> lane-default fallthrough (class empty)
#   - non-worker role is a no-op (class empty)
#   - planning pre-claim (#2197): plan=1 election, --plan-assign claim walk
#     (grant / held-fallthrough / exit-3 --replan / all-held / game --repo
#     namespacing / dry-run+review-only gating) against a stubbed fleet-claim
#   - FLEET_MODEL_* unset -> standalone alias-default fallback resolves each
#     class to its fleet-common.sh default (fable[1m]/opus[1m]/sonnet)
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

# --- T18: semantic-conflict-only slice dispatches opus -----------------------
# The #2417 starvation shape end-to-end: no feedback, no claimable tasks, no
# needs-plan — just a conflicted PR the scout surfaced. The lane must elect
# opus (role-worker step 1c is opus+-only), not defer and not fall through to
# the lane default (a sonnet iteration skips step 1c by design).
echo "T18: semantic-conflict-only slice dispatches opus (step-1c pressure)"
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[],"semantic_conflict_prs":[{"number":2417,"repo":"engine","labels":["fleet:semantic-conflict"]}]}'
assert_eq "$(resolve worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=xhigh more=0 defer=0 count=1 plan=0" \
    "conflicted PR alone elects opus with count=1"

# --- T19: FLEET_MODEL_* unset -> fleet-common.sh alias-default fallback -------
# T1-T18 pin FLEET_MODEL_FABLE/OPUS/SONNET (lines ~60-62), so the
# ${FLEET_MODEL_*:-...} arms in fleet-dispatcher's standalone model resolution
# always short-circuit and the alias-default fallback never runs. Unset the
# whole table — plus the pre-class legacy OPUS_MODEL/SONNET_MODEL fallthroughs —
# so each class resolves THROUGH the fallback to the fleet-common.sh alias
# default (FLEET_FABLE_CANDIDATES_DEFAULT[0] / FLEET_{OPUS,SONNET}_CLASS_DEFAULT).
# solo-architect's MODEL= line uses the byte-identical fable expansion;
# test_solo_architect_model.sh covers that consumer end-to-end.
echo "T19: FLEET_MODEL_* unset resolves to fleet-common.sh alias defaults"
resolve_unpinned() { # $1 = task model class
    write_slice worker "{\"tasks_open\":[{\"issue\":\"#10\",\"model\":\"$1\",\"effort\":null,\"owner\":\"free\",\"blocked\":false}],\"feedback_prs\":[],\"needs_plan\":[]}"
    env -u FLEET_MODEL_FABLE -u FLEET_MODEL_OPUS -u FLEET_MODEL_SONNET \
        -u OPUS_MODEL -u SONNET_MODEL "$DISPATCHER" --resolve-class worker
}
assert_eq "$(resolve_unpinned fable)" \
    "class=fable model=fable[1m] effort=xhigh more=0 defer=0 count=1 plan=0" \
    "unpinned fable resolves to FLEET_FABLE_CANDIDATES_DEFAULT[0]=fable[1m]"
assert_eq "$(resolve_unpinned opus)" \
    "class=opus model=opus[1m] effort=xhigh more=0 defer=0 count=1 plan=0" \
    "unpinned opus resolves to FLEET_OPUS_CLASS_DEFAULT=opus[1m]"
assert_eq "$(resolve_unpinned sonnet)" \
    "class=sonnet model=sonnet effort=high more=0 defer=0 count=1 plan=0" \
    "unpinned sonnet resolves to FLEET_SONNET_CLASS_DEFAULT=sonnet"

# --- T20+: class fairness floor (#2699) ---------------------------------------
#
# The measured defect shape: the elected class's claimable items are ones every
# worker REFUSES, so they never leave tasks_open, while the workers that walked
# and declined them age past CLAIM_SETTLE_SECONDS and stop counting as racing.
# `claim_headroom = DISPATCH_COUNT - class_racing` therefore stays permanently
# positive and the `serving next class` fan-out at the saturation branch is
# unreachable — 57 opus / 0 sonnet / 0 fable dispatches over 2h in the wild.
#
# Reproduced here by pinning CLAIM_SETTLE_SECONDS=0 (nothing is ever "recent",
# so class_racing is always 0 — the same end state as every record aging out)
# against a slice with more opus items than the tick can cover. The assertions
# below run whole dispatch_role ticks via --dispatch-role, not just the
# resolver, so the positive case observes a non-elected class ACTUALLY
# DISPATCHED rather than merely a counter incrementing.
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"; mkdir -p "$FLEET_RESERVATIONS_DIR"
export FLEET_DISPATCH_MIN_GAP_SECONDS=0     # no stagger between the ticks
export FLEET_DISPATCHER_CLAIM_SETTLE_SECONDS=0
export FLEET_CONCURRENCY_WORKER=5           # > the tick count, so the role cap never gates
export FLEET_SESSION="fleet-test-$$"

# Five idle pool panes, each on its own worktree (count_active_for_role dedupes
# by worktree). pgrep exits 1 so no pane reads as running a wrapper.
cat > "$STUB_BIN/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes)
        for i in 1 2 3 4 5; do printf '%%%s|pool|zsh\n' "$i"; done
        exit 0
        ;;
    display-message)
        pane=""; fmt=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -t) pane="$2"; shift 2 ;;
                -p) fmt="$2"; shift 2 ;;
                *)  shift ;;
            esac
        done
        if [[ "$fmt" == *pane_current_path* ]]; then
            echo "/fake/worktrees/pool-${pane#%}"
        elif [[ "$fmt" == *pane_pid* ]]; then
            echo "1"
        fi
        exit 0
        ;;
    send-keys) exit 0 ;;
    *) exit 0 ;;
esac
TMUXEOF
chmod +x "$STUB_BIN/tmux"
cat > "$STUB_BIN/pgrep" <<'PGREPEOF'
#!/usr/bin/env bash
exit 1
PGREPEOF
chmod +x "$STUB_BIN/pgrep"

# One opus task (the refused-but-permanently-counted head) + one sonnet task
# (the starved lane). DISPATCH_COUNT=1 with class_racing pinned to 0 makes
# claim_headroom permanently 1 — positive, so the saturation fan-out never
# fires — while capping each tick's fan-out at a single pane, which is what
# makes ticks countable in the assertions below.
MONOPOLY_SLICE='{"tasks_open":[
  {"issue":"#10","model":"opus","effort":null,"owner":"free","blocked":false},
  {"issue":"#13","model":"sonnet","effort":null,"owner":"free","blocked":false}],
 "feedback_prs":[],"needs_plan":[]}'
OPUS_ONLY_SLICE='{"tasks_open":[
  {"issue":"#10","model":"opus","effort":null,"owner":"free","blocked":false}],
 "feedback_prs":[],"needs_plan":[]}'

# Run <count> consecutive worker ticks from a clean dispatch dir + fresh
# trigger, and print the dispatcher log (stderr) for assertion.
run_ticks() { # $1 = tick count, rest = env assignments
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json
    mkdir -p "$FLEET_STATE_DIR/triggers"
    : > "$FLEET_STATE_DIR/triggers/worker"
    write_slice worker "$MONOPOLY_SLICE"
    local n="$1"; shift
    env "$@" "$DISPATCHER" --dispatch-role worker "$n" 2>&1 >/dev/null
}

echo "T20: negative control — a single tick still dispatches the elected class"
# Scoped to ONE tick deliberately: the floor yields on turn count, so a
# multi-tick soak would (correctly) yield and read as a regression. One tick is
# the shape that pins "the settle-window intent at fleet-dispatcher:470-481 is
# preserved — a healthy elected class is served, not withheld".
out=$(run_ticks 1)
case "$out" in
    *"dispatching worker -> %1 [class=opus"*)
        PASS=$((PASS+1)); echo "  ok: elected opus dispatched on the first tick" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: first tick did not dispatch opus:"; printf '%s\n' "$out" ;;
esac
case "$out" in
    *"fairness floor"*) FAIL=$((FAIL+1)); echo "  FAIL: floor fired on the first tick" ;;
    *) PASS=$((PASS+1)); echo "  ok: no yield before the run threshold" ;;
esac

echo "T21: positive fire — the 4th tick dispatches the NON-elected class"
out=$(run_ticks 4)
case "$out" in
    *"dispatching worker -> "*"[class=sonnet"*)
        PASS=$((PASS+1)); echo "  ok: sonnet (never elected) actually dispatched" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: no sonnet dispatch in 4 ticks:"; printf '%s\n' "$out" ;;
esac
case "$out" in
    *"class=opus elected 3 consecutive ticks with other classes queued; yielding one pass (fairness floor)"*)
        PASS=$((PASS+1)); echo "  ok: yield logged with class and run length" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: fairness-yield log line missing:"; printf '%s\n' "$out" ;;
esac
# The headroom-based fan-out must NOT be what produced it — that branch is
# exactly the one the defect makes unreachable in this slice shape.
case "$out" in
    *"serving next class"*)
        FAIL=$((FAIL+1)); echo "  FAIL: saturation fan-out fired; the defect shape is not reproduced" ;;
    *) PASS=$((PASS+1)); echo "  ok: sonnet came from the floor, not the (unreachable) headroom fan-out" ;;
esac
assert_eq "$(printf '%s\n' "$out" | grep -c 'dispatching worker -> .*class=opus')" "3" \
    "opus served exactly its 3 turns before the yield"

echo "T22: FLEET_DISPATCHER_CLASS_FAIRNESS_RUN=0 disables the floor"
out=$(run_ticks 5 FLEET_DISPATCHER_CLASS_FAIRNESS_RUN=0)
case "$out" in
    *"[class=sonnet"*) FAIL=$((FAIL+1)); echo "  FAIL: floor fired while disabled" ;;
    *) PASS=$((PASS+1)); echo "  ok: run=0 restores the pre-#2699 headroom-only behaviour" ;;
esac

echo "T23: a non-numeric threshold falls back to 3 instead of killing the daemon"
out=$(run_ticks 4 FLEET_DISPATCHER_CLASS_FAIRNESS_RUN=banana)
case "$out" in
    *"CLASS_FAIRNESS_RUN=banana is not a non-negative integer; falling back to 3"*)
        PASS=$((PASS+1)); echo "  ok: invalid override warned and clamped" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: no clamp warning:"; printf '%s\n' "$out" ;;
esac
case "$out" in
    *"[class=sonnet"*) PASS=$((PASS+1)); echo "  ok: clamped default still yields on the 4th tick" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: clamped-to-3 floor did not fire" ;;
esac

echo "T24: single-class slice never accumulates a run (more=0 resets it)"
write_slice worker "$OPUS_ONLY_SLICE"
rm -f "$FLEET_STATE_DIR/dispatch"/*.json
: > "$FLEET_STATE_DIR/triggers/worker"
out=$("$DISPATCHER" --dispatch-role worker 5 2>&1 >/dev/null)
case "$out" in
    *"fairness floor"*) FAIL=$((FAIL+1)); echo "  FAIL: floor fired with no other class to serve" ;;
    *) PASS=$((PASS+1)); echo "  ok: opus-only slice serves opus indefinitely — no monopoly to break" ;;
esac

echo "T25: the floor never turns a servable tick into a deferred one"
# The class the run accrued against is gone by the time the yield fires (its
# task got claimed between ticks — the scout rewrites the slice constantly, so
# this is a live daemon state, not a contrived one). The yield must drop its
# own exclusion and serve the elected class anyway rather than defer the tick.
# Simulated at the send-keys seam: the 3rd dispatch is when "another worker
# claimed the sonnet task", so the stub rewrites the slice to drop it.
export STUB_SLICE_AFTER_3="$TMPROOT/slice-after-3.json"
printf '%s\n' "$OPUS_ONLY_SLICE" > "$STUB_SLICE_AFTER_3"
export STUB_SEND_COUNT="$TMPROOT/send-count"
cat > "$STUB_BIN/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes)
        for i in 1 2 3 4 5; do printf '%%%s|pool|zsh\n' "$i"; done
        exit 0
        ;;
    display-message)
        pane=""; fmt=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -t) pane="$2"; shift 2 ;;
                -p) fmt="$2"; shift 2 ;;
                *)  shift ;;
            esac
        done
        if [[ "$fmt" == *pane_current_path* ]]; then
            echo "/fake/worktrees/pool-${pane#%}"
        elif [[ "$fmt" == *pane_pid* ]]; then
            echo "1"
        fi
        exit 0
        ;;
    send-keys)
        # Optional world-changes-under-us seam (T25): after N dispatches,
        # swap in a slice where the other class's work is gone.
        if [[ -n "${STUB_SLICE_AFTER_3:-}" && -n "${STUB_SEND_COUNT:-}" ]]; then
            n=$(( $(cat "$STUB_SEND_COUNT" 2>/dev/null || echo 0) + 1 ))
            printf '%s' "$n" > "$STUB_SEND_COUNT"
            (( n >= 3 )) && cp "$STUB_SLICE_AFTER_3" "$FLEET_STATE_DIR/projections/worker.json"
        fi
        exit 0
        ;;
    *) exit 0 ;;
esac
TMUXEOF
chmod +x "$STUB_BIN/tmux"
rm -f "$FLEET_STATE_DIR/dispatch"/*.json "$STUB_SEND_COUNT"
: > "$FLEET_STATE_DIR/triggers/worker"
write_slice worker "$MONOPOLY_SLICE"
out=$("$DISPATCHER" --dispatch-role worker 4 2>&1 >/dev/null)
case "$out" in
    *"fairness yield of class=opus found no other servable class; serving it after all"*)
        PASS=$((PASS+1)); echo "  ok: vanished alternative detected, exclusion dropped" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: no fallback log line:"; printf '%s\n' "$out" ;;
esac
assert_eq "$(printf '%s\n' "$out" | grep -c 'dispatching worker -> .*class=opus')" "4" \
    "all four ticks dispatched opus — the yield degraded to serving it, not to a defer"
case "$out" in
    *"deferring trigger"*|*"no claimable work"*)
        FAIL=$((FAIL+1)); echo "  FAIL: the yield deferred a servable tick" ;;
    *) PASS=$((PASS+1)); echo "  ok: no tick deferred" ;;
esac
unset STUB_SLICE_AFTER_3 STUB_SEND_COUNT

echo "T26: --dispatch-role argument validation"
"$DISPATCHER" --dispatch-role >/dev/null 2>&1 \
    && { FAIL=$((FAIL+1)); echo "  FAIL: missing role exited zero"; } \
    || { PASS=$((PASS+1)); echo "  ok: missing role exits non-zero"; }
"$DISPATCHER" --dispatch-role worker 0 >/dev/null 2>&1 \
    && { FAIL=$((FAIL+1)); echo "  FAIL: count=0 exited zero"; } \
    || { PASS=$((PASS+1)); echo "  ok: non-positive count exits non-zero"; }

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]

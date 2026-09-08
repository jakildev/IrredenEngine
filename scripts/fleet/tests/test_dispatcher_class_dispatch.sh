#!/usr/bin/env bash
# Tests for fleet-dispatcher's per-task model-class resolution
# (--resolve-class, backed by resolve_worker_class + fleet_task_class.py).
#
# Covers:
#   - fable task resolves to the fable model at effort high (work default)
#   - fable cap reached -> lane serves the next non-fable task (more=1)
#   - only cap-blocked fable work -> defer (keep trigger, no dispatch)
#   - per-task Effort: override threads through to the dispatch
#   - feedback severity routing (nits-only -> sonnet beats queued tasks)
#   - semantic-conflict-only slice -> opus dispatch (step-1c pressure, #2417)
#   - empty slice -> lane-default fallthrough (class empty)
#   - non-worker role is a no-op (class empty)
#   - planning pre-claim (#2197): plan=1 election, --assign claim walk
#     (grant / held-fallthrough / exit-3 --replan / all-held / game --repo
#     namespacing / dry-run+review-only gating) against a stubbed fleet-claim
#   - FLEET_MODEL_* unset -> standalone alias-default fallback resolves each
#     class to its fleet-common.sh default (fable[1m]/opus[1m]/sonnet)
#   - assignment before launch (T20+): whole --dispatch-role ticks against a
#     stubbed tmux — a launch carries its pre-claimed target, a refused head
#     yields to the next class in the same tick (what retired the #2699
#     fairness floor), all-refused stands the lane down, one launch per
#     claimable item, in-flight dedup, every lane kind's claim arm, the
#     reviewer lane's one-pane-per-PR fan-out, and dry-run's claim-free path
#
# The fable in-flight count comes from dispatch records under
# $FLEET_STATE_DIR/dispatch, same records --count-active reads.

set -euo pipefail
unset FLEET_RUNTIMES FLEET_CROSS_PROVIDER_REVIEW FLEET_WORKER_RUNTIME

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"

if [[ ! -x "$DISPATCHER" ]]; then
    echo "test setup: fleet-dispatcher not found at $DISPATCHER" >&2
    exit 1
fi

# PASS/FAIL, ok/bad, assert_eq and `summarize` come from the shared helper:
# its "passed: N  failed: M" line is what fleet-positive-control scores, and
# the hand-rolled "PASS: n FAIL: m" tally this replaces read as an aborted run.
# shellcheck source=scripts/fleet/tests/lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

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

# --- T1: fable task -> fable model at the high work default -----------------
echo "T1: fable task resolves to fable model"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"fable","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(resolve worker)" \
    "class=fable model=claude-fable-5[1m] effort=high more=0 defer=0 count=1 plan=0" \
    "uncapped fable task dispatches on fable at the high work default"

# --- T2: fable cap reached -> next non-fable task ---------------------------
echo "T2: fable cap diverts to the next class"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"fable","effort":null,"owner":"free","blocked":false},{"issue":"#11","model":"opus","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
printf '{"role":"worker","pane":"%%9","class":"fable","dispatched_at":"x"}\n' \
    > "$FLEET_STATE_DIR/dispatch/pane-9.json"
assert_eq "$(resolve worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=high more=0 defer=0 count=1 plan=0" \
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
    "class=opus model=claude-opus-4-8[1m] effort=high more=1 defer=0 count=1 plan=0" \
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
# dispatch. Exercised via the --assign hook, which runs the same
# resolve + assign_for_pane path a live tick does. fleet-claim is stubbed
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
case "$sub" in
    *-release|release) exit 0 ;;
    reservation-role)
        printf '%s\n' "${STUB_RESERVATION_ROLE:-}"
        exit 0
        ;;
    planning-claim) ;;
    *)
        # Every other lane claim (claim / amending-claim / resolving-claim /
        # review-claim) is granted unless STUB_REFUSE names its key — the
        # assignment tests below drive refusal explicitly, and the fairness
        # ticks (T20+) need every pane's task claim to succeed.
        [[ " ${STUB_REFUSE:-} " == *" $key "* ]] && exit 1
        exit 0
        ;;
esac
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
    "$DISPATCHER" --assign worker worker-9
}

echo "T9: needs-plan slice resolves plan=1 on the elected class"
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[{"number":99,"repo":"engine","labels":[]},{"number":120,"repo":"engine","labels":[]},{"number":7,"repo":"game","labels":[]}]}'
assert_eq "$(resolve worker)" \
    "class=fable model=claude-fable-5[1m] effort=xhigh more=0 defer=0 count=1 plan=1" \
    "untagged needs-plan elects fable with plan=1"

echo "T10: assignment granted on the first candidate, claimed under the agent"
assert_eq "$(STUB_GRANT='engine:99' plan_assign)" "target=plan:engine:99" \
    "first candidate claim granted -> assigned"
grep -q '^planning-claim 99 worker-9$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: claim ran under the pane worktree basename"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: claim argv wrong: $(cat "$FLEET_CLAIM_LOG")"; }

echo "T11: held candidate falls through to the next line (lost race != burned dispatch)"
assert_eq "$(STUB_GRANT='engine:120' plan_assign)" "target=plan:engine:120" \
    "engine:99 held elsewhere (exit 1) -> engine:120 assigned"

echo "T12: exit-3 + live needs-plan retries with --replan and assigns"
assert_eq "$(STUB_PLANNED='engine:99' STUB_REPLAN_GRANT='engine:99' plan_assign)" "target=plan:engine:99" \
    "stale-slice/plan-review re-plan state -> assigned via --replan"
grep -q '^planning-claim 99 worker-9 --replan$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: --replan retry issued"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: no --replan retry: $(cat "$FLEET_CLAIM_LOG")"; }

echo "T13: exit-3 then replan exit-2 (genuinely done) -> next line"
assert_eq "$(STUB_PLANNED='engine:99' STUB_GRANT='engine:120' plan_assign)" "target=plan:engine:120" \
    "already-planned candidate skipped; next line assigned"

echo "T14: all candidates held/planned -> no assignment"
assert_eq "$(plan_assign)" "target=" \
    "every claim refused -> target= (the lane stands down)"

echo "T15: game-repo candidate claims with --repo game before the subcommand"
assert_eq "$(STUB_GRANT='game:7' plan_assign)" "target=plan:game:7" \
    "engine lines held -> game line assigned"
grep -q -- '^--repo game planning-claim 7 worker-9$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: game claim namespaced with --repo game"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: game claim argv wrong: $(cat "$FLEET_CLAIM_LOG")"; }

echo "T16: dry-run / review-only never pre-claim"
printf 'dry-run\n' > "$FLEET_STATE_DIR/dispatch-mode"
assert_eq "$(STUB_GRANT='engine:99' plan_assign)" "target=" \
    "dry-run mode -> no assignment"
[[ ! -s "$FLEET_CLAIM_LOG" ]] \
    && { PASS=$((PASS+1)); echo "  ok: no fleet-claim call in dry-run"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: dry-run still called fleet-claim: $(cat "$FLEET_CLAIM_LOG")"; }
printf 'review-only\n' > "$FLEET_STATE_DIR/dispatch-mode"
assert_eq "$(STUB_GRANT='engine:99' plan_assign)" "target=" \
    "review-only mode -> no assignment"
rm -f "$FLEET_STATE_DIR/dispatch-mode"

echo "T17: a target-carrying dispatch command appends the 7th target= arg"
# build_dispatch_command is exercised via --print-dispatch-command for the
# 6-arg (no assignment) shape; the 7-arg shape is asserted through the log of
# a live-shaped assignment (T20) + the wrap-side export test
# (test_dispatch_wrap_session.sh). Here: no assignment -> 6 args, no target=.
out=$("$DISPATCHER" --print-dispatch-command worker pane-3)
case "$out" in
    *" target="*) FAIL=$((FAIL+1)); echo "  FAIL: unassigned dispatch carries target=: $out" ;;
    *) PASS=$((PASS+1)); echo "  ok: unassigned dispatch has no target= arg" ;;
esac

# --- T18: semantic-conflict-only slice dispatches opus -----------------------
# The #2417 starvation shape end-to-end: no feedback, no claimable tasks, no
# needs-plan — just a conflicted PR the scout surfaced. The lane must elect
# opus (role-worker step 1c is opus+-only), not defer and not fall through to
# the lane default (a sonnet iteration skips step 1c by design).
echo "T18: semantic-conflict-only slice dispatches opus (step-1c pressure)"
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[],"semantic_conflict_prs":[{"number":2417,"repo":"engine","labels":["fleet:semantic-conflict"]}]}'
assert_eq "$(resolve worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=high more=0 defer=0 count=1 plan=0" \
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
    "class=fable model=fable[1m] effort=high more=0 defer=0 count=1 plan=0" \
    "unpinned fable resolves to FLEET_FABLE_CANDIDATES_DEFAULT[0]=fable[1m]"
assert_eq "$(resolve_unpinned opus)" \
    "class=opus model=opus[1m] effort=high more=0 defer=0 count=1 plan=0" \
    "unpinned opus resolves to FLEET_OPUS_CLASS_DEFAULT=opus[1m]"
assert_eq "$(resolve_unpinned sonnet)" \
    "class=sonnet model=sonnet effort=high more=0 defer=0 count=1 plan=0" \
    "unpinned sonnet resolves to FLEET_SONNET_CLASS_DEFAULT=sonnet"

# --- T20+: assignment before launch — whole dispatch_role ticks ---------------
# The dispatcher binds each launched pane to ONE pre-claimed item
# (assign_for_pane): a pane launches only behind a granted lane claim, two
# panes never share an item, an elected class whose candidates are all refused
# yields to the next class in the SAME tick (the #2699 monopoly, caught at the
# claim instead of bounded by a turn count), and a lane with nothing claimable
# consumes its trigger instead of fanning out. These run whole ticks via
# --dispatch-role against a stubbed tmux (five idle pool panes) and the
# fleet-claim stub above, which grants every lane claim unless STUB_REFUSE
# names the key.
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"; mkdir -p "$FLEET_RESERVATIONS_DIR"
export FLEET_DISPATCH_MIN_GAP_SECONDS=0     # no stagger between panes/ticks
export FLEET_DISPATCHER_CLAIM_SETTLE_SECONDS=0
export FLEET_CONCURRENCY_WORKER=5           # > the pane count, so the role cap never gates
export FLEET_SESSION="fleet-test-$$"

# Five idle pool panes, each on its own worktree. pgrep exits 1 so no pane
# reads as running a wrapper. send-keys records what each pane was sent, so
# the target= arg of every launch is assertable.
export SEND_LOG="$TMPROOT/send-keys.log"
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
    send-keys) printf '%s\n' "$*" >> "$SEND_LOG"; exit 0 ;;
    *) exit 0 ;;
esac
TMUXEOF
chmod +x "$STUB_BIN/tmux"
cat > "$STUB_BIN/pgrep" <<'PGREPEOF'
#!/usr/bin/env bash
exit 1
PGREPEOF
chmod +x "$STUB_BIN/pgrep"

TWO_CLASS_SLICE='{"tasks_open":[
  {"issue":"#10","model":"opus","effort":null,"owner":"free","blocked":false,"repo":"engine"},
  {"issue":"#13","model":"sonnet","effort":null,"owner":"free","blocked":false,"repo":"engine"}],
 "feedback_prs":[],"needs_plan":[]}'

# Run <count> consecutive <role> ticks from a clean dispatch dir + fresh
# trigger, and print the dispatcher log (stderr) for assertion.
tick() { # $1 = role, $2 = tick count, rest = env assignments
    local role="$1" n="$2"; shift 2
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json
    : > "$FLEET_CLAIM_LOG"; : > "$SEND_LOG"
    mkdir -p "$FLEET_STATE_DIR/triggers"
    : > "$FLEET_STATE_DIR/triggers/$role"
    env "$@" "$DISPATCHER" --dispatch-role "$role" "$n" 2>&1 >/dev/null
}
count_dispatches() { printf '%s\n' "$1" | grep -c 'dispatching '; }

echo "T20: a launch carries its pre-claimed target; the claim precedes send-keys"
write_slice worker "$TWO_CLASS_SLICE"
out=$(tick worker 1)
case "$out" in
    *"dispatching worker -> %1 [class=opus effort=high target=task:engine:10]"*)
        PASS=$((PASS+1)); echo "  ok: opus elected, task:engine:10 assigned to the first pane" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: no target-bound opus dispatch:"; printf '%s\n' "$out" ;;
esac
grep -q '^claim 10 pool-1$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: claim taken under the pane's worktree basename"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: claim argv wrong: $(cat "$FLEET_CLAIM_LOG")"; }
grep -q 'target=task:engine:10' "$SEND_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: the pane command carries target=task:engine:10"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: send-keys lacks the target: $(cat "$SEND_LOG")"; }
grep -q '"target":"task:engine:10"' "$FLEET_STATE_DIR/dispatch/pane-1.json" \
    && { PASS=$((PASS+1)); echo "  ok: dispatch record stamped with the target"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: record lacks target: $(cat "$FLEET_STATE_DIR"/dispatch/*.json)"; }

echo "T21: a refused head yields to the next class in the SAME tick"
out=$(tick worker 1 STUB_REFUSE='engine:10')
case "$out" in
    *"class=opus has no claimable candidate left; serving next class"*)
        PASS=$((PASS+1)); echo "  ok: re-election logged" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: no re-election line:"; printf '%s\n' "$out" ;;
esac
case "$out" in
    *"dispatching worker -> %1 [class=sonnet effort=high target=task:engine:13]"*)
        PASS=$((PASS+1)); echo "  ok: sonnet served on the first tick, not after a turn count" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: sonnet not dispatched:"; printf '%s\n' "$out" ;;
esac
assert_eq "$(count_dispatches "$out")" "1" "exactly one launch"
grep -q '^claim 10 pool-1$' "$FLEET_CLAIM_LOG" && grep -q '^claim 13 pool-1$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: the refused claim was attempted before the granted one"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: claim sequence wrong: $(cat "$FLEET_CLAIM_LOG")"; }

echo "T22: every candidate refused -> no launch, trigger consumed"
out=$(tick worker 1 STUB_REFUSE='engine:10 engine:13')
assert_eq "$(count_dispatches "$out")" "0" "nothing launched"
case "$out" in
    *"no candidate could be claimed"*"standing down"*)
        PASS=$((PASS+1)); echo "  ok: stand-down logged" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: no stand-down line:"; printf '%s\n' "$out" ;;
esac
[[ ! -f "$FLEET_STATE_DIR/triggers/worker" ]] \
    && { PASS=$((PASS+1)); echo "  ok: trigger consumed (no per-tick claim re-walk)"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: trigger left standing"; }

echo "T23: one launch per claimable item — five idle panes, two opus tasks"
write_slice worker '{"tasks_open":[
  {"issue":"#10","model":"opus","effort":null,"owner":"free","blocked":false,"repo":"engine"},
  {"issue":"#11","model":"opus","effort":null,"owner":"free","blocked":false,"repo":"engine"}],
 "feedback_prs":[],"needs_plan":[]}'
out=$(tick worker 1)
assert_eq "$(count_dispatches "$out")" "2" "two launches for two items (not five for five panes)"
case "$out" in
    *"target=task:engine:10]"*"target=task:engine:11]"*)
        PASS=$((PASS+1)); echo "  ok: distinct targets" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: targets not distinct:"; printf '%s\n' "$out" ;;
esac

echo "T24: an in-flight target is never handed to a second pane"
write_slice worker '{"tasks_open":[
  {"issue":"#10","model":"opus","effort":null,"owner":"free","blocked":false,"repo":"engine"}],
 "feedback_prs":[],"needs_plan":[]}'
rm -f "$FLEET_STATE_DIR/dispatch"/*.json
printf '{"role":"worker","pane":"%%9","class":"opus","dispatched_at":"x","dispatched_epoch":1,"claim_marker":1,"target":"task:engine:10"}\n' \
    > "$FLEET_STATE_DIR/dispatch/pane-9.json"
: > "$FLEET_CLAIM_LOG"; : > "$FLEET_STATE_DIR/triggers/worker"
out=$("$DISPATCHER" --dispatch-role worker 1 2>&1 >/dev/null)
assert_eq "$(count_dispatches "$out")" "0" "the only item is in flight -> nothing launched"
[[ ! -s "$FLEET_CLAIM_LOG" ]] \
    && { PASS=$((PASS+1)); echo "  ok: no claim attempted on an in-flight item"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: claim attempted: $(cat "$FLEET_CLAIM_LOG")"; }
rm -f "$FLEET_STATE_DIR/dispatch/pane-9.json"

echo "T25: every lane kind claims through its own fleet-claim arm (--assign)"
assign() { : > "$FLEET_CLAIM_LOG"; "$DISPATCHER" --assign "$1" pool-3; }
write_slice worker '{"tasks_open":[{"issue":"#7","model":"opus","owner":"free","blocked":false,"repo":"game"}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(assign worker)" "target=task:game:7" "game task -> task:game:7"
grep -q -- '^--repo game claim 7 pool-3$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: game claim namespaced with --repo game"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: game claim argv: $(cat "$FLEET_CLAIM_LOG")"; }
write_slice worker '{"tasks_open":[{"issue":"#344","model":"sonnet","owner":"free","blocked":true,"repo":"engine","stackable_blocker_pr":{"number":397,"headRefName":"claude/324-x"}}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(assign worker)" "target=stack:engine:344:397" "stackable blocked task -> stack target with its base PR"
grep -q -- '^claim 344 pool-3 --stackable-on 397$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: stack claim passes --stackable-on <base>"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: stack claim argv: $(cat "$FLEET_CLAIM_LOG")"; }
write_slice worker '{"tasks_open":[],"feedback_prs":[{"number":50,"repo":"engine","labels":["fleet:approved","fleet:has-nits"]}],"needs_plan":[]}'
assert_eq "$(assign worker)" "target=feedback:engine:50" "feedback PR -> feedback target"
grep -q '^amending-claim 50 pool-3$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: feedback claims via amending-claim"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: feedback claim argv: $(cat "$FLEET_CLAIM_LOG")"; }
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[],"semantic_conflict_prs":[{"number":2417,"repo":"engine","labels":["fleet:semantic-conflict"]}]}'
assert_eq "$(assign worker)" "target=conflict:engine:2417" "conflicted PR -> conflict target"
grep -q '^resolving-claim 2417 pool-3$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: conflict claims via resolving-claim"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: conflict claim argv: $(cat "$FLEET_CLAIM_LOG")"; }
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[{"number":99,"repo":"engine","labels":[]}]}'
assert_eq "$(STUB_GRANT='engine:99' assign worker)" "target=plan:engine:99" "needs-plan issue -> plan target (the #2197 lane)"
write_slice sonnet-reviewer '{"candidate_prs":[{"number":3074,"repo":"engine","labels":[]},{"number":12,"repo":"game","labels":[]}]}'
assert_eq "$(assign sonnet-reviewer)" "target=review:engine:3074" "sonnet-reviewer -> review target"
grep -q '^review-claim 3074 pool-3$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: reviewer claims via review-claim"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: review claim argv: $(cat "$FLEET_CLAIM_LOG")"; }
assert_eq "$(STUB_REFUSE='engine:3074' assign sonnet-reviewer)" "target=review:game:12" \
    "held PR -> the next candidate, namespaced"
write_slice opus-reviewer '{"flagged_prs":[],"plan_review":[{"number":605,"repo":"engine","labels":[]}]}'
assert_eq "$(assign opus-reviewer)" "target=planreview:engine:605" "opus-reviewer plan-review issue -> planreview target"
grep -q '^review-claim 605 pool-3$' "$FLEET_CLAIM_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: plan review claims the issue via review-claim"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: planreview claim argv: $(cat "$FLEET_CLAIM_LOG")"; }
assert_eq "$(assign merger)" "target=" "merger is not target-bound (legacy batch pass)"

echo "T25b: a reviewer lane fans out one pane per candidate and goes quiet on an empty slice"
write_slice sonnet-reviewer '{"candidate_prs":[{"number":3074,"repo":"engine","labels":[]},{"number":3080,"repo":"engine","labels":[]}]}'
out=$(tick sonnet-reviewer 1)
assert_eq "$(count_dispatches "$out")" "2" "two candidate PRs -> two launches (five panes idle)"
case "$out" in
    *"[target=review:engine:3074]"*"[target=review:engine:3080]"*)
        PASS=$((PASS+1)); echo "  ok: each launch carries its own PR" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: review targets:"; printf '%s\n' "$out" ;;
esac
write_slice sonnet-reviewer '{"candidate_prs":[]}'
out=$(tick sonnet-reviewer 1)
assert_eq "$(count_dispatches "$out")" "0" "empty candidate list -> no launch (the 96-no-op night)"
[[ ! -f "$FLEET_STATE_DIR/triggers/sonnet-reviewer" ]] \
    && { PASS=$((PASS+1)); echo "  ok: trigger consumed"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: trigger left standing"; }

echo "T25c: dry-run launches the standby path without a claim or a target"
write_slice worker "$TWO_CLASS_SLICE"
printf 'dry-run\n' > "$FLEET_STATE_DIR/dispatch-mode"
out=$(tick worker 1)
rm -f "$FLEET_STATE_DIR/dispatch-mode"
[[ ! -s "$FLEET_CLAIM_LOG" ]] \
    && { PASS=$((PASS+1)); echo "  ok: no claim in dry-run"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: dry-run claimed: $(cat "$FLEET_CLAIM_LOG")"; }
if grep -q 'target=' "$SEND_LOG"; then
    FAIL=$((FAIL+1)); echo "  FAIL: dry-run launch carries a target: $(cat "$SEND_LOG")"
else
    PASS=$((PASS+1)); echo "  ok: dry-run launch carries no target"
fi

echo "T26: --dispatch-role argument validation"
"$DISPATCHER" --dispatch-role >/dev/null 2>&1 \
    && { FAIL=$((FAIL+1)); echo "  FAIL: missing role exited zero"; } \
    || { PASS=$((PASS+1)); echo "  ok: missing role exits non-zero"; }
"$DISPATCHER" --dispatch-role worker 0 >/dev/null 2>&1 \
    && { FAIL=$((FAIL+1)); echo "  FAIL: count=0 exited zero"; } \
    || { PASS=$((PASS+1)); echo "  ok: non-positive count exits non-zero"; }

# --- T29+: idle-fallthrough gate --------------------------------------------
# A worker slice that EXISTS but routes '' (here: the only task is owned by
# another worker) must stand the lane down instead of fanning the lane
# default out to every idle pane — unless a reservation is waiting (T31), or
# the slice file is missing entirely (T30, the scout-not-up shape).
OWNED_SLICE='{"tasks_open":[{"issue":"#10","model":"opus","effort":null,"owner":"pool-9","blocked":false}],"feedback_prs":[],"needs_plan":[]}'

echo "T29: existing slice, nothing claimable, no reservation -> stand down"
rm -f "$FLEET_STATE_DIR/dispatch"/*.json
: > "$FLEET_STATE_DIR/triggers/worker"
write_slice worker "$OWNED_SLICE"
out=$("$DISPATCHER" --dispatch-role worker 1 2>&1 >/dev/null)
case "$out" in
    *"dispatching worker"*) FAIL=$((FAIL+1)); echo "  FAIL: idle lane still dispatched: $out" ;;
    *) PASS=$((PASS+1)); echo "  ok: no dispatch on an unclaimable slice" ;;
esac
case "$out" in
    *"standing down"*) PASS=$((PASS+1)); echo "  ok: stand-down logged" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: stand-down log line missing: $out" ;;
esac
[[ ! -f "$FLEET_STATE_DIR/triggers/worker" ]] \
    && { PASS=$((PASS+1)); echo "  ok: trigger consumed (scout re-arms on new work)"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: trigger left standing"; }

echo "T30: missing slice waits for an assignment"
rm -f "$FLEET_STATE_DIR/dispatch"/*.json "$FLEET_STATE_DIR/projections/worker.json"
: > "$FLEET_STATE_DIR/triggers/worker"
out=$("$DISPATCHER" --dispatch-role worker 1 2>&1 >/dev/null)
case "$out" in
    *"dispatching worker"*) FAIL=$((FAIL+1)); echo "  FAIL: launched without assignment data" ;;
    *) PASS=$((PASS+1)); echo "  ok: waits for assignment data" ;;
esac

echo "T31: a standing worker reservation keeps the '' dispatch (resume path)"
rm -f "$FLEET_STATE_DIR/dispatch"/*.json
: > "$FLEET_STATE_DIR/triggers/worker"
write_slice worker "$OWNED_SLICE"
mkdir -p "$FLEET_RESERVATIONS_DIR"
printf '{"task":"#10","role":"worker"}\n' > "$FLEET_RESERVATIONS_DIR/pool-2.json"
out=$(STUB_RESERVATION_ROLE=worker "$DISPATCHER" --dispatch-role worker 1 2>&1 >/dev/null)
case "$out" in
    *"dispatching worker"*) PASS=$((PASS+1)); echo "  ok: reservation resume still dispatches through the gate" ;;
    *) FAIL=$((FAIL+1)); echo "  FAIL: reservation resume was gated off: $out" ;;
esac
rm -f "$FLEET_RESERVATIONS_DIR/pool-2.json"

# --- T32+: per-target dispatch cap (the planning circuit breaker, generalized)
# gh is stubbed so park_target's label add + comment are observable.
export GH_LOG="$TMPROOT/gh.log"
cat > "$STUB_BIN/gh" <<'GHEOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$GH_LOG"
exit 0
GHEOF
chmod +x "$STUB_BIN/gh"
COUNTS_DIR="$FLEET_STATE_DIR/target-dispatch-counts"
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[{"number":99,"repo":"engine","labels":[]},{"number":120,"repo":"engine","labels":[]}]}'

echo "T32: candidate at the cap is parked and the next line assigned"
rm -rf "$COUNTS_DIR"; mkdir -p "$COUNTS_DIR"; : > "$GH_LOG"
printf '2' > "$COUNTS_DIR/plan-engine-99"
assert_eq "$(FLEET_TARGET_DISPATCH_CAP=2 STUB_GRANT='engine:120' plan_assign)" "target=plan:engine:120" \
    "plan:engine:99 at cap -> parked, engine:120 assigned"
# #3034 park semantics: ADD fleet:needs-human only — fleet:needs-plan stays
# on (re-entry = the human removing the park label).
grep -q 'api repos/jakildev/IrredenEngine/issues/99/labels --method POST -f labels\[\]=fleet:needs-human' "$GH_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: parked by adding fleet:needs-human"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: park label add missing: $(cat "$GH_LOG")"; }
grep -q 'needs-plan' "$GH_LOG" \
    && { FAIL=$((FAIL+1)); echo "  FAIL: park touched fleet:needs-plan (must stay per #3034): $(cat "$GH_LOG")"; } \
    || { PASS=$((PASS+1)); echo "  ok: fleet:needs-plan kept (the #3034 contract)"; }
grep -q 'api repos/jakildev/IrredenEngine/issues/99/comments -f body=Dispatch circuit breaker: 2 dispatches of `plan:engine:99`' "$GH_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: park comment posted, naming the target and count"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: park comment missing: $(cat "$GH_LOG")"; }
[[ ! -f "$COUNTS_DIR/plan-engine-99" ]] \
    && { PASS=$((PASS+1)); echo "  ok: parked target's counter cleared"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: counter left after park"; }

echo "T33: a granted assignment increments the per-target counter"
rm -rf "$COUNTS_DIR"; : > "$GH_LOG"
assert_eq "$(STUB_GRANT='engine:99' plan_assign)" "target=plan:engine:99" "assignment granted"
assert_eq "$(cat "$COUNTS_DIR/plan-engine-99" 2>/dev/null)" "1" "counter recorded one dispatch"
assert_eq "$(STUB_GRANT='engine:99' plan_assign)" "target=plan:engine:99" "second assignment granted"
assert_eq "$(cat "$COUNTS_DIR/plan-engine-99" 2>/dev/null)" "2" "counter incremented"
[[ ! -s "$GH_LOG" ]] \
    && { PASS=$((PASS+1)); echo "  ok: no gh call below the cap"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: gh called below the cap: $(cat "$GH_LOG")"; }

echo "T34: FLEET_TARGET_DISPATCH_CAP=0 disables the breaker"
rm -rf "$COUNTS_DIR"; mkdir -p "$COUNTS_DIR"; : > "$GH_LOG"
printf '99' > "$COUNTS_DIR/plan-engine-99"
assert_eq "$(FLEET_TARGET_DISPATCH_CAP=0 STUB_GRANT='engine:99' plan_assign)" "target=plan:engine:99" \
    "cap=0 -> assignment proceeds regardless of count"
[[ ! -s "$GH_LOG" ]] \
    && { PASS=$((PASS+1)); echo "  ok: cap=0 never parks"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: cap=0 still called gh: $(cat "$GH_LOG")"; }

echo "T35: a task target is capped the same way — parked on its issue, the next task assigned"
rm -rf "$COUNTS_DIR"; mkdir -p "$COUNTS_DIR"; : > "$GH_LOG"
write_slice worker '{"tasks_open":[{"issue":"#7","model":"opus","owner":"free","blocked":false,"repo":"game"},{"issue":"#8","model":"opus","owner":"free","blocked":false,"repo":"game"}],"feedback_prs":[],"needs_plan":[]}'
printf '3' > "$COUNTS_DIR/task-game-7"
assert_eq "$(FLEET_TARGET_DISPATCH_CAP=3 assign worker)" "target=task:game:8" \
    "task:game:7 at cap -> parked, task:game:8 assigned"
grep -q 'api repos/jakildev/irreden/issues/7/labels --method POST -f labels\[\]=fleet:needs-human' "$GH_LOG" \
    && { PASS=$((PASS+1)); echo "  ok: task parked on its own repo's issue"; } \
    || { FAIL=$((FAIL+1)); echo "  FAIL: task park label add missing: $(cat "$GH_LOG")"; }
grep -q -- '--repo game claim 7 pool-3' "$FLEET_CLAIM_LOG" \
    && { FAIL=$((FAIL+1)); echo "  FAIL: capped task was still claimed: $(cat "$FLEET_CLAIM_LOG")"; } \
    || { PASS=$((PASS+1)); echo "  ok: capped task never reaches fleet-claim"; }
assert_eq "$(cat "$COUNTS_DIR/task-game-8" 2>/dev/null)" "1" "the assigned task's counter started"
rm -rf "$COUNTS_DIR"


echo "T36: a Codex pin launches only after claim, with its actual model"
cat > "$STUB_BIN/codex" <<'EOF'
#!/usr/bin/env bash
echo "test stub must never launch a model" >&2
exit 99
EOF
chmod +x "$STUB_BIN/codex"
export FLEET_RUNTIMES=claude,codex
write_slice worker '{"tasks_open":[{"issue":"#910","model":"fable","owner":"free","blocked":false,"labels":["fleet:runtime-codex"]}]}'
out=$(tick worker 1)
assert_contains "$(cat "$SEND_LOG")" "gpt-6-astra xhigh worker" "Astra receives design-class work"
assert_contains "$(cat "$SEND_LOG")" "target=task:engine:910 codex fable" "target, provider and class reach wrapper"
assert_contains "$(cat "$FLEET_CLAIM_LOG")" "claim 910" "concrete job claimed first"

echo "T37: Codex cooldown preserves the trigger without taking a claim"
mkdir -p "$FLEET_STATE_DIR/runtime-cooldown"
printf '{"until":9999999999}' > "$FLEET_STATE_DIR/runtime-cooldown/codex.json"
out=$(tick worker 1)
[[ ! -s "$SEND_LOG" && ! -s "$FLEET_CLAIM_LOG" && -f "$FLEET_STATE_DIR/triggers/worker" ]]     && ok "unavailable provider neither claims nor consumes the edge"     || bad "cooldown lost the trigger or claimed work"

echo "T37b: a quota-blocked class yields to available work on the other provider"
write_slice worker '{"tasks_open":[{"issue":"#910","model":"fable","owner":"free","blocked":false,"labels":["fleet:runtime-codex"]},{"issue":"#911","model":"sonnet","owner":"free","blocked":false,"labels":["fleet:runtime-claude"]}]}'
out=$(tick worker 1)
assert_contains "$(cat "$SEND_LOG")" "target=task:engine:911 claude sonnet" "Codex quota does not starve Claude work"
write_slice worker '{"tasks_open":[{"issue":"#910","model":"fable","owner":"free","blocked":false,"labels":["fleet:runtime-codex"]}]}'
rm -f "$FLEET_STATE_DIR/runtime-cooldown/codex.json"

echo "T38: Claude-only mixed-fleet host honors a Codex pin"
export FLEET_RUNTIMES=claude
out=$(tick worker 1)
[[ ! -s "$SEND_LOG" && ! -s "$FLEET_CLAIM_LOG" ]]     && ok "pin is not silently replaced by Claude" || bad "pin bypassed"
unset FLEET_RUNTIMES

summarize "fleet-dispatcher class-dispatch tests"

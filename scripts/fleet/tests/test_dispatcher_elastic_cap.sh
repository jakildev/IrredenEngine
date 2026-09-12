#!/usr/bin/env bash
# Tests for fleet-dispatcher's elastic per-role concurrency cap
# (FLEET_CAP_MODE=elastic|strict, default elastic).
#
# Covers, via whole --dispatch-role ticks against a stubbed tmux / pgrep /
# fleet-claim (hermetic — scripts/fleet/CLAUDE.md):
#   - a role at cap launches into a free pane when no other role is pending,
#     and the dispatch line is stamped over-cap=<cap>
#   - one free pane is held back per OTHER pending role with cap headroom
#   - the launch is refused when it would leave such a role without a pane;
#     the trigger is kept
#   - another pending role that is itself at cap reserves nothing
#   - FLEET_CAP_MODE=strict refuses over-cap regardless (env, conf, env>conf)
#   - an invalid FLEET_CAP_MODE falls back to elastic with a warning
#   - the fable cap still holds in elastic mode (cap-blocked fable defers; a
#     non-fable task is served over cap instead)
#   - a lane under cap fills its cap first, then spreads over it next tick
#   - the reservation binds an UNDER-cap lane too, across a sequential
#     multi-role tick (worker at cap, sonnet reviewer cap 4, opus reviewer
#     cap 1): every pending under-cap role still gets a pane

set -euo pipefail
unset FLEET_RUNTIMES FLEET_CROSS_PROVIDER_REVIEW FLEET_WORKER_RUNTIME FLEET_CAP_MODE

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"

if [[ ! -x "$DISPATCHER" ]]; then
    echo "SKIP: fleet-dispatcher not found at $DISPATCHER" >&2
    exit 3
fi

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
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_ALERTS_DIR="$TMPROOT/alerts"
export FLEET_SESSION="fleet-test-$$"
mkdir -p "$FLEET_STATE_DIR/projections" "$FLEET_STATE_DIR/dispatch" \
    "$FLEET_STATE_DIR/triggers" "$FLEET_SESSIONS_DIR" "$FLEET_RESERVATIONS_DIR" \
    "$FLEET_ALERTS_DIR"
: > "$FLEET_CONF"

# Pin the class table so assertions don't depend on host env. cap=1 on both
# lanes under test; fable cap 1 so one in-flight fable iteration saturates it.
export FLEET_MODEL_FABLE='claude-fable-5[1m]'
export FLEET_MODEL_OPUS='claude-opus-4-8[1m]'
export FLEET_MODEL_SONNET='sonnet'
export FLEET_CONCURRENCY_MODEL_FABLE=1
export FLEET_CONCURRENCY_WORKER=1
export FLEET_CONCURRENCY_SONNET_REVIEWER=1
export FLEET_DISPATCH_MIN_GAP_SECONDS=0
export FLEET_DISPATCHER_CLAIM_SETTLE_SECONDS=0
export FLEET_DISPATCHER_BOOT_FANOUT_WINDOW_SECONDS=0

STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
export PATH="$STUB_BIN:$PATH"

# fleet-claim grants every lane claim; reservation-role answers from env.
cat > "$STUB_BIN/fleet-claim" <<'EOF'
#!/usr/bin/env bash
if [[ "${1:-}" == "--repo" ]]; then shift 2; fi
case "${1:-}" in
    reservation-role) printf '%s\n' "${STUB_RESERVATION_ROLE:-}" ;;
esac
exit 0
EOF
chmod +x "$STUB_BIN/fleet-claim"

# Three pool panes on their own worktrees. BUSY_PANES (space-separated ids,
# e.g. "%2 %3") report `claude` in the foreground; the rest sit at an idle
# shell. pgrep exits 1 so no idle pane reads as running a wrapper.
export BUSY_PANES=""
# Pool size the tmux stub reports. Three panes is enough for the
# single-role cases; the sequential multi-role case (T12) widens it.
export POOL_PANES=3
export SEND_LOG="$TMPROOT/send-keys.log"
cat > "$STUB_BIN/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes)
        for (( i = 1; i <= ${POOL_PANES:-3}; i++ )); do
            cmd=zsh
            [[ " ${BUSY_PANES:-} " == *" %$i "* ]] && cmd=claude
            printf '%%%s|pool|%s\n' "$i" "$cmd"
        done
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

write_slice() { printf '%s\n' "$2" > "$FLEET_STATE_DIR/projections/$1.json"; }

# An in-flight <role> iteration of <class> on pane %<n> (worktree pool-<n>).
inflight() { # $1 = role, $2 = class, $3 = target, $4 = pane number
    printf '{"role":"%s","pane":"%%%s","class":"%s","dispatched_at":"x","dispatched_epoch":1,"claim_marker":1,"target":"%s"}\n' \
        "$1" "$4" "$2" "$3" > "$FLEET_STATE_DIR/dispatch/pane-$4.json"
}

# Fresh state for a case: no triggers, no conf, and only an in-flight worker
# on %3 — which is what holds the cap=1 worker lane AT its cap.
reset() {
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json "$FLEET_STATE_DIR/triggers"/* \
        "$FLEET_STATE_DIR/projections"/*.json
    : > "$FLEET_CONF"
    POOL_PANES=3
    inflight worker opus task:engine:10 3
}

# <n> consecutive worker ticks from a fresh trigger; prints the dispatcher
# log (stderr) for assertion.
tick() { # $1 = tick count, rest = env assignments
    local n="$1"; shift
    : > "$SEND_LOG"
    : > "$FLEET_STATE_DIR/triggers/worker"
    env "$@" "$DISPATCHER" --dispatch-role worker "$n" 2>&1 >/dev/null
}
# One tick of <role>, with the triggers already on disk (the caller sets
# them). Separate processes stand in for the live tick's sequential walk of
# DISPATCHED_ROLES: every cross-role signal the admission rule reads — the
# dispatch records each launch writes, the trigger files — is on disk in
# FLEET_STATE_DIR, so the second role sees exactly what it would in-process.
tick_role() { # $1 = role, rest = env assignments
    local role="$1"; shift
    env "$@" "$DISPATCHER" --dispatch-role "$role" 1 2>&1 >/dev/null
}
count_dispatches() { printf '%s\n' "$1" | grep -c 'dispatching '; }
trigger_kept() { [[ -f "$FLEET_STATE_DIR/triggers/worker" ]]; }

ONE_TASK='{"tasks_open":[
  {"issue":"#11","model":"opus","effort":null,"owner":"free","blocked":false,"repo":"engine"}],
 "feedback_prs":[],"needs_plan":[]}'
TWO_TASKS='{"tasks_open":[
  {"issue":"#11","model":"opus","effort":null,"owner":"free","blocked":false,"repo":"engine"},
  {"issue":"#12","model":"opus","effort":null,"owner":"free","blocked":false,"repo":"engine"}],
 "feedback_prs":[],"needs_plan":[]}'
OVER_CAP_LINE='dispatching worker -> %1 [class=opus effort=high target=task:engine:11 over-cap=1] runtime=claude'

echo "T1: at cap, free panes, nothing else pending -> over-cap launch"
reset
write_slice worker "$ONE_TASK"
out=$(tick 1 BUSY_PANES='%3')
assert_eq "$(count_dispatches "$out")" "1" "one launch past the cap"
assert_contains "$out" "$OVER_CAP_LINE" "dispatch line stamped over-cap=<cap> inside the bracket"
assert_absent "$out" "at concurrency cap" "no cap deferral logged"
grep -q 'target=task:engine:11' "$SEND_LOG" \
    && ok "the pane command carries the assigned target" \
    || bad "send-keys lacks the target: $(cat "$SEND_LOG")"

echo "T2: one free pane is held back per other pending under-cap role"
reset
write_slice worker "$TWO_TASKS"
: > "$FLEET_STATE_DIR/triggers/sonnet-reviewer"
out=$(tick 1 BUSY_PANES='%3')
assert_eq "$(count_dispatches "$out")" "1" "two free panes, one reserved for the reviewer -> one launch"
assert_contains "$out" "over-cap=1" "the launch that did happen is over cap"
trigger_kept && ok "worker trigger kept for the pane the reviewer leaves" \
    || bad "worker trigger consumed"

echo "T3: refused when the only free pane is reserved for a pending under-cap role"
reset
write_slice worker "$ONE_TASK"
: > "$FLEET_STATE_DIR/triggers/sonnet-reviewer"
out=$(tick 1 BUSY_PANES='%2 %3')
assert_eq "$(count_dispatches "$out")" "0" "no launch"
assert_contains "$out" \
    "worker at concurrency cap (1 >= 1); 1 free pane(s) reserved for other pending roles; deferring trigger" \
    "deferral names the reservation"
trigger_kept && ok "trigger kept for the next tick" || bad "trigger consumed"

echo "T4: a pending role that is itself at cap reserves nothing"
reset
write_slice worker "$ONE_TASK"
: > "$FLEET_STATE_DIR/triggers/sonnet-reviewer"
inflight sonnet-reviewer sonnet review:engine:3103 2
out=$(tick 1 BUSY_PANES='%2 %3')
assert_eq "$(count_dispatches "$out")" "1" "reviewer at its own cap -> worker takes the free pane"
assert_contains "$out" "$OVER_CAP_LINE" "over-cap launch into the one free pane"

echo "T5: FLEET_CAP_MODE=strict (env) refuses over-cap regardless"
reset
write_slice worker "$ONE_TASK"
out=$(tick 1 BUSY_PANES='%3' FLEET_CAP_MODE=strict)
assert_eq "$(count_dispatches "$out")" "0" "no launch under strict"
assert_contains "$out" "worker at concurrency cap (1 >= 1); deferring trigger" "ceiling deferral logged"
assert_absent "$out" "reserved for other pending roles" "strict never reasons about reservations"
trigger_kept && ok "trigger kept" || bad "trigger consumed"

echo "T6: FLEET_CAP_MODE=strict from the conf file"
reset
write_slice worker "$ONE_TASK"
printf 'FLEET_CAP_MODE=strict\n' > "$FLEET_CONF"
assert_eq "$("$DISPATCHER" --print-cap-mode)" "strict" "conf sets the mode"
out=$(tick 1 BUSY_PANES='%3')
assert_eq "$(count_dispatches "$out")" "0" "conf strict refuses the over-cap launch"

echo "T7: env FLEET_CAP_MODE beats the conf"
assert_eq "$(FLEET_CAP_MODE=elastic "$DISPATCHER" --print-cap-mode)" "elastic" "env wins over conf"
out=$(tick 1 BUSY_PANES='%3' FLEET_CAP_MODE=elastic)
assert_eq "$(count_dispatches "$out")" "1" "env elastic launches over the conf's strict"

echo "T8: default and invalid values"
reset
assert_eq "$("$DISPATCHER" --print-cap-mode)" "elastic" "unset -> elastic"
assert_eq "$(FLEET_CAP_MODE=bogus "$DISPATCHER" --print-cap-mode 2>/dev/null)" "elastic" \
    "invalid value falls back to elastic"
# `|| true`: a pre-fix dispatcher rejects the arm with a non-zero exit, and the
# positive control needs the suite to reach summarize on that ref too.
warn=$(FLEET_CAP_MODE=bogus "$DISPATCHER" --print-cap-mode 2>&1 >/dev/null) || true
assert_contains "$warn" "FLEET_CAP_MODE=bogus is not elastic|strict; falling back to elastic" \
    "invalid value is warned once at startup"

echo "T9: the fable cap still holds in elastic mode"
reset
inflight worker fable task:engine:10 3
write_slice worker '{"tasks_open":[
  {"issue":"#11","model":"fable","effort":null,"owner":"free","blocked":false,"repo":"engine"}],
 "feedback_prs":[],"needs_plan":[]}'
out=$(tick 1 BUSY_PANES='%3')
assert_eq "$(count_dispatches "$out")" "0" "cap-blocked fable is not launched over the role cap"
assert_contains "$out" "no claimable work (cap-blocked fable" "the fable defer is what stops it"
trigger_kept && ok "trigger kept for when the fable slot frees" || bad "trigger consumed"

echo "T10: over-cap launch serves the non-fable task while fable is capped"
reset
inflight worker fable task:engine:10 3
write_slice worker '{"tasks_open":[
  {"issue":"#11","model":"fable","effort":null,"owner":"free","blocked":false,"repo":"engine"},
  {"issue":"#12","model":"opus","effort":null,"owner":"free","blocked":false,"repo":"engine"}],
 "feedback_prs":[],"needs_plan":[]}'
out=$(tick 1 BUSY_PANES='%3')
assert_eq "$(count_dispatches "$out")" "1" "one launch"
assert_contains "$out" "[class=opus effort=high target=task:engine:12 over-cap=1]" "opus served over cap"
assert_absent "$out" "class=fable" "fable never launched past its own cap"

echo "T11: under cap the lane fills its cap first, then spreads over it next tick"
reset
rm -f "$FLEET_STATE_DIR/dispatch"/*.json
write_slice worker "$TWO_TASKS"
out=$(tick 2)
assert_eq "$(count_dispatches "$out")" "2" "two ticks, two launches (cap 1, three free panes)"
first=$(printf '%s\n' "$out" | grep 'dispatching ' | head -n 1) || true
second=$(printf '%s\n' "$out" | grep 'dispatching ' | tail -n 1) || true
assert_contains "$first" "dispatching worker -> %1 [class=opus effort=high target=task:engine:11] runtime=claude" \
    "first tick fills the cap slot with no over-cap stamp"
assert_contains "$second" "dispatching worker -> %2 [class=opus effort=high target=task:engine:12 over-cap=1] runtime=claude" \
    "second tick launches the next task over cap"

echo "T12: sequential multi-role tick — every pending under-cap role keeps a pane"
# The reservation is only worth what the NEXT role in the tick honours. Four
# panes: %4 holds the in-flight worker (cap 1, so that lane is at cap), %1-%3
# free. Worker, sonnet-reviewer (cap 4) and opus-reviewer (cap 1) all have a
# standing trigger, and the dispatcher walks them in that DISPATCHED_ROLES
# order. Each lane is given more claimable work than the panes it may take,
# so what bounds a launch count here is the admission rule, not the slice.
reset
POOL_PANES=4
rm -f "$FLEET_STATE_DIR/dispatch"/*.json
inflight worker opus task:engine:10 4
write_slice worker "$TWO_TASKS"
write_slice sonnet-reviewer '{"candidate_prs":[
  {"number":3001,"repo":"engine","labels":[]},
  {"number":3002,"repo":"engine","labels":[]}]}'
write_slice opus-reviewer '{"flagged_prs":[
  {"number":3003,"repo":"engine","labels":[]}],"plan_review":[]}'
: > "$FLEET_STATE_DIR/triggers/worker"
: > "$FLEET_STATE_DIR/triggers/sonnet-reviewer"
: > "$FLEET_STATE_DIR/triggers/opus-reviewer"

out=$(tick_role worker BUSY_PANES='%4' POOL_PANES=4)
assert_eq "$(count_dispatches "$out")" "1" \
    "worker at cap takes one of three free panes (two reserved for the reviewers)"
assert_contains "$out" "over-cap=1" "the worker launch is over cap"

out=$(tick_role sonnet-reviewer BUSY_PANES='%4' POOL_PANES=4 FLEET_CONCURRENCY_SONNET_REVIEWER=4)
assert_eq "$(count_dispatches "$out")" "1" \
    "under-cap sonnet reviewer (cap 4, 2 free panes, 2 PRs) still leaves the opus reviewer a pane"
assert_absent "$out" "over-cap=" "an under-cap launch is not stamped over-cap"

out=$(tick_role opus-reviewer BUSY_PANES='%4' POOL_PANES=4)
assert_eq "$(count_dispatches "$out")" "1" \
    "the opus reviewer gets the pane the reservation held for it"

echo "T13: the under-cap clamp never falls below one pane"
# Two under-cap roles, one free pane, each reserving for the other: whoever
# the tick reaches first must still launch. Without the entitlement floor
# both defer and the pane sits idle forever.
reset
POOL_PANES=4
rm -f "$FLEET_STATE_DIR/dispatch"/*.json
inflight worker opus task:engine:10 2
inflight worker opus task:engine:13 3
write_slice sonnet-reviewer '{"candidate_prs":[
  {"number":3001,"repo":"engine","labels":[]},
  {"number":3002,"repo":"engine","labels":[]}]}'
write_slice opus-reviewer '{"flagged_prs":[
  {"number":3003,"repo":"engine","labels":[]}],"plan_review":[]}'
: > "$FLEET_STATE_DIR/triggers/sonnet-reviewer"
: > "$FLEET_STATE_DIR/triggers/opus-reviewer"
out=$(tick_role sonnet-reviewer BUSY_PANES='%2 %3 %4' POOL_PANES=4 FLEET_CONCURRENCY_SONNET_REVIEWER=4)
assert_eq "$(count_dispatches "$out")" "1" \
    "one free pane, one other pending under-cap role -> the first role still launches"

echo "T14: the cross-role service order — one free pane, reviewer and worker both pending"
# Both lanes are under cap and both have a standing trigger; one pane is free.
# Whoever the tick reaches first takes it (T13), so the order of
# DISPATCHED_ROLES is what decides between a review and new work.
t14_setup() {
    reset
    POOL_PANES=4
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json
    inflight worker opus task:engine:10 4
    write_slice worker "$ONE_TASK"
    write_slice sonnet-reviewer '{"candidate_prs":[
      {"number":3001,"repo":"engine","labels":[]}]}'
    : > "$FLEET_STATE_DIR/triggers/worker"
    : > "$FLEET_STATE_DIR/triggers/sonnet-reviewer"
}
T14_ENV=(BUSY_PANES='%2 %3 %4' POOL_PANES=4 FLEET_CONCURRENCY_WORKER=4 FLEET_CONCURRENCY_SONNET_REVIEWER=2)

# (a) walked worker-first by hand: the worker takes the pane, the reviewer
# finds none.
t14_setup
out_w=$(tick_role worker "${T14_ENV[@]}")
out_r=$(tick_role sonnet-reviewer "${T14_ENV[@]}")
assert_contains "$out_w" "dispatching worker -> %1" \
    "walked worker-first, the worker takes the only free pane"
assert_eq "$(count_dispatches "$out_r")" "0" \
    "walked worker-first, the reviewer is left without a pane"

# (b) the real tick walks DISPATCHED_ROLES: the reviewer takes the pane and
# the worker trigger waits for the next one.
t14_setup
out=$(env "${T14_ENV[@]}" "$DISPATCHER" --dispatch-tick 1 2>&1 >/dev/null)
assert_eq "$(count_dispatches "$out")" "1" \
    "the tick launches exactly one lane into the one free pane"
assert_contains "$out" "dispatching sonnet-reviewer -> %1" \
    "the shipped order hands the pane to the reviewer"
assert_absent "$out" "dispatching worker" \
    "the worker lane does not launch this tick"
trigger_kept && ok "the worker trigger is kept for the next tick" \
    || bad "the worker trigger was consumed without a launch"

summarize "fleet-dispatcher elastic cap tests"

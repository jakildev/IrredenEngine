#!/usr/bin/env bash
# fleet-dispatcher: a usage window that meters one model family gates only
# launches on that family.
#
# Covers, against a latched Fable-only weekly wall
# (`seven_day_overage_included` rejected) and the real class resolver and
# claim walk (`--resolve-class`, `--assign`, fleet-claim stubbed):
#   - the worker lane serves the walled fable class like a saturated fable
#     cap: a fable-only task slice defers, a fable task yields to the opus
#     task behind it, and a fable-tagged plan degrades from fable to opus
#   - a claude-routed sonnet reviewer and an opus task still claim
#   - an `account` scope override turns the same wall back into a
#     Claude-wide one: nothing is claimed
#   - a claude-only host (no FLEET_RUNTIMES) gates the claim walk on the
#     window scoped to the launch model: an opus wall holds an opus task and
#     leaves a sonnet task running
#   - a reserved Claude worker is gated on the model its sidecar resumes,
#     not the model the current slice resolves: a Fable session holds at the
#     Fable wall while the slice serves opus (whole --dispatch-role ticks
#     against a stubbed tmux)
#   - controls: with no wall latched the fable class is served as fable; an
#     opus sidecar, or a sidecar the wrapper would not resume, launches

set -euo pipefail
unset FLEET_RUNTIMES FLEET_CROSS_PROVIDER_REVIEW FLEET_WORKER_RUNTIME FLEET_MODEL_FABLE_PROBED

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

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/scoped-gate.XXXXXX")
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
mkdir -p "$FLEET_SESSIONS_DIR" "$FLEET_STATE_DIR/projections" \
    "$FLEET_STATE_DIR/dispatch" "$FLEET_STATE_DIR/usage"
touch "$FLEET_CONF"
for _v in $(compgen -A variable | grep -E '^FLEET_DISPATCHER_USAGE_(GATE|SCOPE)' || true); do
    unset "$_v"
done
unset _v

# Pin the class table so assertions don't depend on host env.
export FLEET_MODEL_FABLE='claude-fable-5[1m]'
export FLEET_MODEL_OPUS='claude-opus-4-8[1m]'
export FLEET_MODEL_SONNET='sonnet'
export FLEET_CONCURRENCY_MODEL_FABLE=2

export FLEET_CLAIM_LOG="$TMPROOT/fleet-claim.log"
STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/fleet-claim" <<'EOF'
#!/usr/bin/env bash
[[ "$1" == reservation-role ]] && { echo worker; exit 0; }
printf '%s\n' "$*" >> "$FLEET_CLAIM_LOG"
EOF
chmod +x "$STUB_BIN/fleet-claim"
export PATH="$STUB_BIN:$PATH"

write_slice() { printf '%s\n' "$2" > "$FLEET_STATE_DIR/projections/$1.json"; }
resolve() { "$DISPATCHER" --resolve-class "$1"; }
assign() { : > "$FLEET_CLAIM_LOG"; "$DISPATCHER" --assign "$1" pool-3; }
claims() { grep -c . "$FLEET_CLAIM_LOG" 2>/dev/null || true; }

RESETS=$(( $(date +%s) + 86400 ))
wall() { # $1 = rate-limit type
    printf '{"rateLimitType":"%s","utilization":1.0,"resetsAt":%s,"observed_at":%s,"status":"rejected"}\n' \
        "$1" "$RESETS" "$(date +%s)" > "$FLEET_STATE_DIR/usage/$1.rejected.json"
}
clear_walls() { rm -f "$FLEET_STATE_DIR/usage"/*.json; }

FABLE_ONLY='{"tasks_open":[{"issue":"#10","model":"fable","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
FABLE_THEN_OPUS='{"tasks_open":[{"issue":"#10","model":"fable","effort":null,"owner":"free","blocked":false},{"issue":"#11","model":"opus","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
PLAN_ONLY='{"tasks_open":[],"feedback_prs":[],"needs_plan":[{"number":99,"repo":"engine","labels":["fleet:fable"]}]}'
OPUS_TASK='{"tasks_open":[{"issue":"#11","model":"opus","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
SONNET_TASK='{"tasks_open":[{"issue":"#12","model":"sonnet","effort":null,"owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
REVIEW='{"candidate_prs":[{"number":3074,"repo":"engine","labels":["fleet:author-codex"]}]}'

echo "T1: control — no wall, the fable class is served as fable"
clear_walls
write_slice worker "$FABLE_ONLY"
assert_eq "$(resolve worker)" \
    "class=fable model=claude-fable-5[1m] effort=high more=0 defer=0 count=1 plan=0" \
    "fable task resolves to the fable model"
write_slice worker "$PLAN_ONLY"
assert_eq "$(resolve worker)" \
    "class=fable model=claude-fable-5[1m] effort=xhigh more=0 defer=0 count=1 plan=1" \
    "fable-tagged plan elects fable"

echo "T2: a Fable-only wall serves the fable class like a saturated cap"
wall seven_day_overage_included
write_slice worker "$FABLE_ONLY"
assert_eq "$(resolve worker)" "class= model= effort= more=0 defer=1 count= plan=0" \
    "fable-only task slice defers instead of launching into the wall"
write_slice worker "$FABLE_THEN_OPUS"
assert_eq "$(resolve worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=high more=0 defer=0 count=1 plan=0" \
    "the fable task waits; the opus task behind it is served"
write_slice worker "$PLAN_ONLY"
assert_eq "$(resolve worker)" \
    "class=opus model=claude-opus-4-8[1m] effort=xhigh more=0 defer=0 count=1 plan=1" \
    "the plan degrades from fable to opus"

echo "T3: the Fable wall leaves Claude launches on other models running"
export FLEET_RUNTIMES=claude
write_slice sonnet-reviewer "$REVIEW"
assert_eq "$(assign sonnet-reviewer)" "target=review:engine:3074" \
    "a claude-routed sonnet review is claimed"
write_slice worker "$OPUS_TASK"
assert_eq "$(assign worker)" "target=task:engine:11" "an opus task is claimed"

echo "T4: an account scope override makes the same wall Claude-wide"
export FLEET_DISPATCHER_USAGE_SCOPE_SEVEN_DAY_OVERAGE_INCLUDED=account
write_slice sonnet-reviewer "$REVIEW"
assert_eq "$(assign sonnet-reviewer)" "target=" "the review is not claimed"
assert_eq "$(claims)" "0" "no fleet-claim call behind the closed gate"
write_slice worker "$OPUS_TASK"
assert_eq "$(assign worker)" "target=" "the opus task is not claimed"
unset FLEET_DISPATCHER_USAGE_SCOPE_SEVEN_DAY_OVERAGE_INCLUDED
unset FLEET_RUNTIMES

echo "T5: claude-only host — the claim walk reads the launch model's window"
clear_walls
wall seven_day_opus
write_slice worker "$OPUS_TASK"
assert_eq "$(assign worker)" "target=" "an opus wall holds the opus task"
assert_eq "$(claims)" "0" "held before the claim"
write_slice worker "$SONNET_TASK"
assert_eq "$(assign worker)" "target=task:engine:12" "a sonnet task runs through the opus wall"
clear_walls
write_slice worker "$OPUS_TASK"
assert_eq "$(assign worker)" "target=task:engine:11" "control: with no wall the opus task is claimed"

echo "T6: a reserved Claude worker is gated on the model it resumes"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_SESSION="fleet-test-$$"
export FLEET_DISPATCH_MIN_GAP_SECONDS=0 BOOT_FANOUT_WINDOW_SECONDS=0
export FLEET_CONCURRENCY_WORKER=1
export SEND_LOG="$TMPROOT/send-keys.log"
mkdir -p "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR/triggers"
cat > "$STUB_BIN/tmux" <<'EOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    list-panes) printf '%%1|pool|zsh\n' ;;
    display-message)
        if [[ "$*" == *pane_current_path* ]]; then
            printf '/fake/.claude/worktrees/pool-1\n'
        elif [[ "$*" == *pane_pid* ]]; then
            printf '1\n'
        fi
        ;;
    send-keys) printf '%s\n' "$*" >> "$SEND_LOG" ;;
esac
exit 0
EOF
printf '#!/usr/bin/env bash\nexit 1\n' > "$STUB_BIN/pgrep"
chmod +x "$STUB_BIN/tmux" "$STUB_BIN/pgrep"
printf '{"task":"#11","role":"worker"}\n' > "$FLEET_RESERVATIONS_DIR/pool-1.json"
TRIGGER="$FLEET_STATE_DIR/triggers/worker"
sidecar() { # $1 = role, $2 = model
    printf '{"session_id":"sid","role":"%s","model":"%s","effort":"high","runtime":"claude"}\n' \
        "$1" "$2" > "$FLEET_SESSIONS_DIR/pool-1.session.json"
}
tick() {
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json
    : > "$SEND_LOG"
    : > "$TRIGGER"
    "$DISPATCHER" --dispatch-role worker 1 2>&1 >/dev/null
}
clear_walls
wall seven_day_overage_included
write_slice worker "$OPUS_TASK"
sidecar worker "$FLEET_MODEL_FABLE"
out=$(tick)
assert_absent "$out" "dispatching worker" "a reserved Fable session does not launch into the Fable wall"
assert_eq "$(grep -c . "$SEND_LOG" || true)" "0" "no keys are sent to the reserved pane"
[[ -f "$TRIGGER" ]] && ok "the held resume keeps the trigger" || bad "the held resume consumed the trigger"
sidecar worker "$FLEET_MODEL_OPUS"
out=$(tick)
assert_contains "$out" "dispatching worker -> %1" "control: a reserved opus session launches through the Fable wall"
sidecar sonnet-reviewer "$FLEET_MODEL_FABLE"
out=$(tick)
assert_contains "$out" "dispatching worker -> %1" \
    "control: a sidecar the wrapper launches fresh is gated on the slice's model"
clear_walls
sidecar worker "$FLEET_MODEL_FABLE"
out=$(tick)
assert_contains "$out" "dispatching worker -> %1" "control: with no wall the reserved Fable session launches"

summarize "fleet-dispatcher model-scoped gate tests"

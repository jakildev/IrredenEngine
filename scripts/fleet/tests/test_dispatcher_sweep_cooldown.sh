#!/usr/bin/env bash
# Tests for the dispatcher's sweep-cooldown deferral.
#
# When `fleet-claim cleanup --gh` removes a PR's amending/resolving claim on
# age alone it stamps `fleet:sweep-cooldown`, and the worker lane withholds
# that PR's feedback / conflict target while the PR's updatedAt is younger
# than FLEET_CLAIM_SWEPT_COOLDOWN_SECS (default 1800): the owner it could not
# vouch for may still be about to push. Pinned here, over whole
# `--dispatch-role worker` ticks against stubbed tmux / fleet-claim:
#   - a cooling PR gets no lane claim, the lane defers rather than falling
#     through to an untargeted launch, and the deferral logs ONCE across ticks;
#   - past the cooldown the same PR is claimed as normal (control);
#   - conflict targets behave the same way;
#   - a cooling PR does not block other claimable work in the same tick.
# The clock is pinned through fleet_task_class's FLEET_TASK_CLASS_NOW seam.

set -euo pipefail
unset FLEET_RUNTIMES FLEET_CROSS_PROVIDER_REVIEW FLEET_WORKER_RUNTIME \
    FLEET_CLAIM_SWEPT_COOLDOWN_SECS FLEET_DISPATCH_ID FLEET_ROLE_MODEL

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
[[ -x "$DISPATCHER" ]] || { echo "SKIP: fleet-dispatcher not found at $DISPATCHER" >&2; exit 3; }
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"; touch "$FLEET_CONF"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_STATE_DIR/projections" "$FLEET_STATE_DIR/dispatch" "$FLEET_STATE_DIR/triggers" \
    "$FLEET_SESSIONS_DIR" "$FLEET_RESERVATIONS_DIR"
export FLEET_MODEL_FABLE='claude-fable-5[1m]'
export FLEET_MODEL_OPUS='claude-opus-4-8[1m]'
export FLEET_MODEL_SONNET='sonnet'
export FLEET_DISPATCH_MIN_GAP_SECONDS=0
export FLEET_DISPATCHER_CLAIM_SETTLE_SECONDS=0
export FLEET_CONCURRENCY_WORKER=5
export FLEET_SESSION="fleet-test-$$"
export FLEET_TEST_HOST=linux

# 1800000000 = 2027-01-15T08:00:00Z.
export FLEET_TASK_CLASS_NOW=1800000000
FIVE_MIN_AGO="2027-01-15T07:55:00Z"
FORTY_MIN_AGO="2027-01-15T07:20:00Z"

export FLEET_CLAIM_LOG="$TMPROOT/fleet-claim.log"
export SEND_LOG="$TMPROOT/send-keys.log"
STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/fleet-claim" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$FLEET_CLAIM_LOG"
case "${1:-}" in
    reservation-role) exit 0 ;;
    *) exit 0 ;;
esac
EOF
cat > "$STUB_BIN/tmux" <<'EOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes) for i in 1 2; do printf '%%%s|pool|zsh\n' "$i"; done ;;
    display-message)
        pane=""; fmt=""
        while [[ $# -gt 0 ]]; do
            case "$1" in -t) pane="$2"; shift 2 ;; -p) fmt="$2"; shift 2 ;; *) shift ;; esac
        done
        if [[ "$fmt" == *pane_current_path* ]]; then echo "/fake/worktrees/pool-${pane#%}"
        elif [[ "$fmt" == *pane_pid* ]]; then echo 1; fi
        ;;
    send-keys) printf '%s\n' "$*" >> "$SEND_LOG" ;;
esac
exit 0
EOF
printf '#!/usr/bin/env bash\nexit 1\n' > "$STUB_BIN/pgrep"
chmod +x "$STUB_BIN/fleet-claim" "$STUB_BIN/tmux" "$STUB_BIN/pgrep"
export PATH="$STUB_BIN:$PATH"

write_slice() { printf '%s\n' "$1" > "$FLEET_STATE_DIR/projections/worker.json"; }
tick() {  # <count> — consecutive ticks in ONE dispatcher process; prints the log
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json
    : > "$FLEET_CLAIM_LOG"; : > "$SEND_LOG"
    : > "$FLEET_STATE_DIR/triggers/worker"
    "$DISPATCHER" --dispatch-role worker "$1" 2>&1 >/dev/null
}
count() { grep -c -- "$2" <<< "$1" || true; }
feedback_pr() {  # <number> <updatedAt> [extra-label]
    printf '{"number":%s,"repo":"engine","updatedAt":"%s","labels":["fleet:needs-fix","fleet:sweep-cooldown"%s]}' \
        "$1" "$2" "${3:+,\"$3\"}"
}
conflict_pr() {
    printf '{"number":%s,"repo":"engine","updatedAt":"%s","labels":["fleet:semantic-conflict","fleet:sweep-cooldown"]}' "$1" "$2"
}

echo "=== T1: a cooling feedback PR is not claimed, the lane defers, the deferral logs once ==="
write_slice "{\"tasks_open\":[],\"needs_plan\":[],\"semantic_conflict_prs\":[],\"feedback_prs\":[$(feedback_pr 50 "$FIVE_MIN_AGO")]}"
assert_eq "$("$DISPATCHER" --resolve-class worker)" \
    "class= model= effort= more=0 defer=1 count= plan=0" \
    "a cooling-only slice defers instead of falling through to an untargeted launch"
out=$(tick 2)
assert_absent "$(cat "$FLEET_CLAIM_LOG")" "amending-claim 50" "no amending-claim on the cooling PR across two ticks"
assert_eq "$(count "$out" 'deferring feedback:engine:50: claim swept 300s ago (cooldown 1800s)')" 1 \
    "exactly one deferral line across two ticks"
assert_eq "$(count "$out" 'dispatching ')" 0 "nothing launched"

echo "=== T2: control — past the cooldown the same PR is claimed ==="
write_slice "{\"tasks_open\":[],\"needs_plan\":[],\"semantic_conflict_prs\":[],\"feedback_prs\":[$(feedback_pr 50 "$FORTY_MIN_AGO")]}"
out=$(tick 1)
assert_contains "$(cat "$FLEET_CLAIM_LOG")" "amending-claim 50 pool-1" "a 40-min-old cooldown no longer withholds the PR"
assert_absent "$out" "deferring feedback:engine:50" "no deferral logged past the cooldown"
assert_contains "$out" "target=feedback:engine:50" "the PR is dispatched as a feedback target"

echo "=== T3: conflict targets defer the same way ==="
write_slice "{\"tasks_open\":[],\"needs_plan\":[],\"feedback_prs\":[],\"semantic_conflict_prs\":[$(conflict_pr 2417 "$FIVE_MIN_AGO")]}"
out=$(tick 2)
assert_absent "$(cat "$FLEET_CLAIM_LOG")" "resolving-claim 2417" "no resolving-claim on a cooling conflict PR"
assert_eq "$(count "$out" 'deferring conflict:engine:2417')" 1 "exactly one conflict deferral line across two ticks"
write_slice "{\"tasks_open\":[],\"needs_plan\":[],\"feedback_prs\":[],\"semantic_conflict_prs\":[$(conflict_pr 2417 "$FORTY_MIN_AGO")]}"
tick 1 >/dev/null
assert_contains "$(cat "$FLEET_CLAIM_LOG")" "resolving-claim 2417 pool-1" "control: past the cooldown the conflict is claimed"

echo "=== T4: a cooling PR does not hold back other work ==="
write_slice "{\"needs_plan\":[],\"semantic_conflict_prs\":[],
  \"feedback_prs\":[$(feedback_pr 50 "$FIVE_MIN_AGO")],
  \"tasks_open\":[{\"issue\":\"#10\",\"model\":\"opus\",\"effort\":null,\"owner\":\"free\",\"blocked\":false,\"repo\":\"engine\"}]}"
out=$(tick 1)
assert_contains "$out" "target=task:engine:10" "the claimable task is dispatched"
assert_absent "$(cat "$FLEET_CLAIM_LOG")" "amending-claim 50" "the cooling PR is still not claimed"
assert_eq "$(count "$out" 'deferring feedback:engine:50')" 1 "the deferral is logged beside the dispatch"

echo "=== T5: a PR without the label is untouched by the gate ==="
write_slice '{"tasks_open":[],"needs_plan":[],"semantic_conflict_prs":[],"feedback_prs":[{"number":51,"repo":"engine","updatedAt":"2027-01-15T07:59:00Z","labels":["fleet:needs-fix"]}]}'
out=$(tick 1)
assert_contains "$(cat "$FLEET_CLAIM_LOG")" "amending-claim 51 pool-1" "a fresh PR with no cooldown label is claimed"
assert_absent "$out" "deferring" "and logs no deferral"

summarize "dispatcher sweep cooldown"

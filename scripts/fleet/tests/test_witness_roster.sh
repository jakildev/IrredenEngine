#!/usr/bin/env bash
# Tests for retired architect heartbeat monitoring.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
WITNESS="${WITNESS:-$SCRIPT_DIR/witness}"
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -x "$WITNESS" ]]; then
    echo "test setup: witness not found (or not executable) at $WITNESS" >&2
    exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/test-witness-roster.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

run_witness() {
    local fake_home="$1" escalate_n="${2:-}"
    mkdir -p "$fake_home/.fleet/heartbeats"
    local -a env_args=(
        -u FLEET_ALERTS_DIR -u FLEET_STATE_DIR -u FLEET_WITNESS_ESCALATE_N
        "HOME=$fake_home"
    )
    if [[ -n "$escalate_n" ]]; then
        env_args+=("FLEET_WITNESS_ESCALATE_N=$escalate_n")
    fi
    env "${env_args[@]}" "$WITNESS" --once > "$fake_home/out.txt" 2>&1
}

# An idle interactive architect is not stale: its heartbeat is deliberately
# older than the retired two-hour threshold and its old alert state is removed.
home_idle="$TMP/home-idle-architect"
mkdir -p "$home_idle/.fleet/heartbeats" "$home_idle/.fleet/alerts" \
    "$home_idle/.fleet/state"
touch -t 202401010000 "$home_idle/.fleet/heartbeats/opus-architect" \
    "$home_idle/.fleet/heartbeats/game-architect"
touch "$home_idle/.fleet/alerts/opus-architect.stuck" \
    "$home_idle/.fleet/alerts/game-architect.stuck" \
    "$home_idle/.fleet/alerts/witness-roster.stuck"
printf '3 0 stale-heartbeat|opus-architect\n' \
    > "$home_idle/.fleet/state/.witness-stale-opus-architect-skip"
printf '3 0 stale-heartbeat|game-architect\n' \
    > "$home_idle/.fleet/state/.witness-stale-game-architect-skip"
printf '3 0 roster-empty|opus-architect game-architect\n' \
    > "$home_idle/.fleet/state/.witness-roster-skip"
run_witness "$home_idle"
out_idle=$(cat "$home_idle/out.txt")

assert_absent "$out_idle" "ROSTER EMPTY" "idle architect: no roster-empty warning"
assert_absent "$out_idle" "STALE: opus-architect" "idle architect: no stale warning"
assert_absent "$out_idle" "STALE: game-architect" "idle game architect: no stale warning"
assert_contains "$out_idle" "all healthy" "idle architect: reports healthy"
for artifact in \
    "$home_idle/.fleet/alerts/opus-architect.stuck" \
    "$home_idle/.fleet/alerts/game-architect.stuck" \
    "$home_idle/.fleet/alerts/witness-roster.stuck" \
    "$home_idle/.fleet/state/.witness-stale-opus-architect-skip" \
    "$home_idle/.fleet/state/.witness-stale-game-architect-skip" \
    "$home_idle/.fleet/state/.witness-roster-skip"; do
    [[ -e "$artifact" ]] \
        && bad "idle architect: retired artifact removed ($artifact)" \
        || ok "idle architect: retired artifact removed ($artifact)"
done

# The dispatcher PID remains the real liveness signal. It must still raise and
# retain a durable alert when the recorded process is dead.
home_dispatcher="$TMP/home-dispatcher"
mkdir -p "$home_dispatcher/.fleet/state"
echo 999999 > "$home_dispatcher/.fleet/state/dispatcher.pid"
dispatcher_alert="$home_dispatcher/.fleet/alerts/fleet-dispatcher.stuck"
dispatcher_counter="$home_dispatcher/.fleet/state/.witness-stale-dispatcher-skip"

run_witness "$home_dispatcher" 2
assert_contains "$(cat "$home_dispatcher/out.txt")" "STALE: fleet-dispatcher" \
    "dead dispatcher: warning emitted"
[[ -f "$dispatcher_alert" && -f "$dispatcher_counter" ]] \
    && ok "dead dispatcher: durable alert and counter written" \
    || bad "dead dispatcher: durable alert and counter written"

run_witness "$home_dispatcher" 2
assert_contains "$(cat "$home_dispatcher/out.txt")" "STALE ESCALATION" \
    "dead dispatcher: escalates at threshold"

rm -f "$dispatcher_alert"
run_witness "$home_dispatcher" 2
assert_absent "$(cat "$home_dispatcher/out.txt")" "STALE: fleet-dispatcher" \
    "dead dispatcher: quiet after escalation"
[[ -f "$dispatcher_alert" ]] \
    && ok "dead dispatcher: alert refreshes after escalation" \
    || bad "dead dispatcher: alert refreshes after escalation"

summarize "witness roster tests"

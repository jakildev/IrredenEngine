#!/usr/bin/env bash
# Tests for fleet-dispatcher's 5-hour-window usage gate.
#
# Covers:
#   - empty usage dir => gate open
#   - utilization >= threshold + fresh + future reset => gate closed
#   - threshold override via env var (FLEET_DISPATCHER_USAGE_GATE)
#   - stale observation (older than USAGE_STALE_SECONDS) => ignored
#   - resetsAt in the past => observation ignored
#   - utilization < threshold => gate open with util reported
#   - worst-of across multiple types when only one is over threshold
#   - defensive percent path (utilization > 1.5 treated as percent)

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

assert_starts_with() {
    local actual="$1" prefix="$2" msg="$3"
    if [[ "$actual" == "$prefix"* ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected prefix: $prefix"
        echo "        actual:          $actual"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
mkdir -p "$FLEET_STATE_DIR/usage"

NOW=$(date +%s)
# Future ISO-8601 timestamp; portable across BSD/GNU date.
RESETS=$(python3 -c "import datetime,time; print(datetime.datetime.fromtimestamp(time.time()+3600,tz=datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))")

echo "T1: empty usage dir => open"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "empty state reports open"

echo "T2: utilization 0.85, fresh, future reset => closed"
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "85% trips default 80% threshold"

echo "T3: same fixture, threshold 0.99 => open"
out=$(FLEET_DISPATCHER_USAGE_GATE=0.99 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:five_hour util=85%" "raised threshold opens gate"

echo "T4: stale observation (2h old) => open"
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$((NOW - 7200))" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "stale observation ignored"

echo "T5: resetsAt in the past (well past grace) => open"
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"2020-01-01T00:00:00Z","observed_at":%s}\n' "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "expired window ignored"

echo "T5b: resetsAt 60s in the past, default 600s grace => still closed"
RECENT_RESET=$(python3 -c "import datetime,time; print(datetime.datetime.fromtimestamp(time.time()-60,tz=datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))")
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RECENT_RESET" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "grace window keeps gate closed past resetsAt"

echo "T5c: same fixture, grace=0 => open immediately"
out=$(FLEET_DISPATCHER_RESET_GRACE_SECONDS=0 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "grace=0 reverts to instant open at resetsAt"

echo "T6: utilization 0.50 => open with util reported"
printf '{"rateLimitType":"five_hour","utilization":0.50,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:five_hour util=50%" "below-threshold reports util"

echo "T7: worst-of across two types (one over)"
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
printf '{"rateLimitType":"output_tokens","utilization":0.30,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/output_tokens.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "worst observation wins"
rm -f "$FLEET_STATE_DIR/usage/output_tokens.json"

echo "T8: defensive percent path (raw 85, not 0.85) => closed"
printf '{"rateLimitType":"five_hour","utilization":85,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "percent value treated as percent"

# --- Per-type thresholds -----------------------------------------------------

echo "T9: seven_day at 92% with builtin 0.95 default => open"
printf '{"rateLimitType":"seven_day","utilization":0.92,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/seven_day.json"
rm -f "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:seven_day util=92%" "seven_day defaults to 0.95"

echo "T10: seven_day at 96% with builtin 0.95 default => closed"
printf '{"rateLimitType":"seven_day","utilization":0.96,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/seven_day.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:seven_day util=96% (>= 95%)" "seven_day trips at 96%"

echo "T11: per-type env override beats builtin default"
out=$(FLEET_DISPATCHER_USAGE_GATE_SEVEN_DAY=0.50 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:seven_day util=96% (>= 50%)" "per-type override wins"

echo "T12: global env override applies to types without per-type override"
# unknown type — falls through per-type lookup -> global override -> 0.50
printf '{"rateLimitType":"daily_tokens","utilization":0.60,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/daily_tokens.json"
rm -f "$FLEET_STATE_DIR/usage/seven_day.json"
out=$(FLEET_DISPATCHER_USAGE_GATE=0.50 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:daily_tokens util=60% (>= 50%)" "global override applies to unknown types"

echo "T13: per-type override beats global override"
# Same daily_tokens at 60%, but global set to 0.50, daily_tokens-specific set to 0.99 -> open
out=$(FLEET_DISPATCHER_USAGE_GATE=0.50 FLEET_DISPATCHER_USAGE_GATE_DAILY_TOKENS=0.99 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:daily_tokens util=60%" "per-type beats global"

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]

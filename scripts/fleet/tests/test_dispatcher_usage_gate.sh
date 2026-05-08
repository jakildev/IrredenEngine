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

echo "T5: resetsAt in the past => open"
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"2020-01-01T00:00:00Z","observed_at":%s}\n' "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "expired window ignored"

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

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]

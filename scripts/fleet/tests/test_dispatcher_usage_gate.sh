#!/usr/bin/env bash
# Tests for fleet-dispatcher's 5-hour-window usage gate.
#
# Covers:
#   - empty usage dir => gate open
#   - utilization >= threshold + fresh + future reset => gate closed
#   - threshold override via env var (FLEET_DISPATCHER_USAGE_GATE)
#   - stale observation (older than USAGE_STALE_SECONDS) w/o resetsAt => ignored
#   - stale observation but future resetsAt => still closed (window authoritative)
#   - resetsAt well in the past (past grace) => observation ignored
#   - resetsAt recently in the past, within RESET_GRACE_SECONDS => still active
#   - RESET_GRACE_SECONDS=0 reverts to instant open at resetsAt
#   - epoch-int resetsAt within grace => still active (parity with ISO branch)
#   - non-numeric RESET_GRACE_SECONDS override falls back to default
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

# Isolate from the operator's ~/.fleet/fleet-up.conf — the dispatcher sources
# it on startup, so a host that sets e.g. FLEET_DISPATCHER_USAGE_GATE_FIVE_HOUR
# would clobber the thresholds these cases assert (per-type beats the global
# override T3 passes). Point FLEET_CONF at an empty file and clear any gate
# vars already in the caller env so the suite tests the baked defaults.
export FLEET_CONF=/dev/null
for _v in $(compgen -A variable | grep '^FLEET_DISPATCHER_USAGE_GATE' || true); do
    unset "$_v"
done
unset _v

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

echo "T4: stale observation (2h old) with NO resetsAt => open"
# Staleness cutoff only applies when there's no reset boundary to trust.
printf '{"rateLimitType":"five_hour","utilization":0.85,"observed_at":%s}\n' "$((NOW - 7200))" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "stale observation without resetsAt ages out"

echo "T4b: stale observation (2h old) but future resetsAt => still closed"
# Regression guard: a future resetsAt is authoritative. Utilization can't
# fall below the wall until the window resets, so an old-but-still-bound
# reading must keep the gate closed. Previously the stale cutoff ran first
# and blinded the gate mid-window, re-triggering a worker into the wall.
printf '{"rateLimitType":"five_hour","utilization":1,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$((NOW - 7200))" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=100%" "future resetsAt overrides stale observed_at"

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

echo "T5d: epoch-int resetsAt 60s in the past, default grace => still closed"
RECENT_RESET_EPOCH=$((NOW - 60))
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":%s,"observed_at":%s}\n' "$RECENT_RESET_EPOCH" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "epoch-int resetsAt also honors grace"

echo "T5e: non-numeric RESET_GRACE_SECONDS falls back to default (still closed)"
# Re-use the T5b ISO fixture (60s past reset) so a working grace keeps the
# gate closed. Pass a typo'd value; the dispatcher must clamp to 600 and
# stay closed. Without the validate-and-clamp guard the inline-Python int()
# raises, the heredoc dies, stdout is empty, and usage_gate_open treats
# empty as "not closed:" — silent gate-open.
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RECENT_RESET" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$(FLEET_DISPATCHER_RESET_GRACE_SECONDS="600s" "$DISPATCHER" --gate-status 2>/dev/null)
assert_starts_with "$out" "closed:five_hour util=85%" "non-numeric grace override falls back to default"

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

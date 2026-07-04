#!/usr/bin/env bash
# Tests for fleet-dispatcher's GitHub API quota gate (#2221, epic #1394 Q3).
#
# Mirrors test_dispatcher_usage_gate.sh's harness against the three
# github_{core,graphql,search} entries in BUILTIN_PER_TYPE_DEFAULTS.
# fleet-state-scout's sample_github_rate_limit() is the writer in
# production; this test drops the fixture files directly so it exercises
# only the dispatcher's evaluator, same as the Anthropic-side suite does.
#
# Covers:
#   - github_graphql at 92% (>= 90% default) => closed
#   - github_search at 100% => open (threshold 1.01, surface-only, never gates)
#   - github_core below its 90% default => open with util reported
#   - a github pool past resetsAt + grace => ignored (open)
#   - github and five_hour observations coexist without cross-perturbing
#     each other's evaluation (worst-of picks whichever actually breaches)

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

# Isolate from the operator's ~/.fleet/fleet-up.conf, same rationale as
# test_dispatcher_usage_gate.sh T-setup: a host-level per-type override
# would clobber the baked-default assertions below.
export FLEET_CONF=/dev/null
for _v in $(compgen -A variable | grep '^FLEET_DISPATCHER_USAGE_GATE' || true); do
    unset "$_v"
done
unset _v

NOW=$(date +%s)
FUTURE_RESET=$((NOW + 3600))

echo "T1: github_graphql at 92% (>= builtin 90%) => closed"
printf '{"rateLimitType":"github_graphql","utilization":0.92,"resetsAt":%s,"observed_at":%s,"limit":5000,"remaining":400}\n' \
    "$FUTURE_RESET" "$NOW" > "$FLEET_STATE_DIR/usage/github-graphql.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:github_graphql util=92% (>= 90%)" "github_graphql trips default 90% threshold"
rm -f "$FLEET_STATE_DIR/usage/github-graphql.json"

echo "T2: github_search at 100% (threshold 1.01, surface-only) => open"
printf '{"rateLimitType":"github_search","utilization":1.0,"resetsAt":%s,"observed_at":%s,"limit":30,"remaining":0}\n' \
    "$FUTURE_RESET" "$NOW" > "$FLEET_STATE_DIR/usage/github-search.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:github_search util=100%" "github_search never gates even at 100%"
rm -f "$FLEET_STATE_DIR/usage/github-search.json"

echo "T3: github_core at 60% (< builtin 90%) => open with util reported"
printf '{"rateLimitType":"github_core","utilization":0.60,"resetsAt":%s,"observed_at":%s,"limit":5000,"remaining":2000}\n' \
    "$FUTURE_RESET" "$NOW" > "$FLEET_STATE_DIR/usage/github-core.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:github_core util=60%" "below-threshold github_core reports util"
rm -f "$FLEET_STATE_DIR/usage/github-core.json"

echo "T4: github_graphql past resetsAt + grace => ignored (open)"
PAST_RESET=$((NOW - 7200))
printf '{"rateLimitType":"github_graphql","utilization":0.99,"resetsAt":%s,"observed_at":%s,"limit":5000,"remaining":10}\n' \
    "$PAST_RESET" "$NOW" > "$FLEET_STATE_DIR/usage/github-graphql.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "expired github window ignored"
rm -f "$FLEET_STATE_DIR/usage/github-graphql.json"

echo "T5: github_core override via per-type env var"
printf '{"rateLimitType":"github_core","utilization":0.60,"resetsAt":%s,"observed_at":%s,"limit":5000,"remaining":2000}\n' \
    "$FUTURE_RESET" "$NOW" > "$FLEET_STATE_DIR/usage/github-core.json"
out=$(FLEET_DISPATCHER_USAGE_GATE_GITHUB_CORE=0.50 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:github_core util=60% (>= 50%)" "per-type override applies to github pools"
rm -f "$FLEET_STATE_DIR/usage/github-core.json"

echo "T6: github and Anthropic observations coexist — worst-of wins"
printf '{"rateLimitType":"github_graphql","utilization":0.70,"resetsAt":%s,"observed_at":%s,"limit":5000,"remaining":1500}\n' \
    "$FUTURE_RESET" "$NOW" > "$FLEET_STATE_DIR/usage/github-graphql.json"
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":%s,"observed_at":%s}\n' \
    "$FUTURE_RESET" "$NOW" > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "five_hour breach wins when github pool is under its own threshold"
rm -f "$FLEET_STATE_DIR/usage/github-graphql.json" "$FLEET_STATE_DIR/usage/five_hour.json"

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]

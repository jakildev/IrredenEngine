#!/usr/bin/env bash
# Tests for the periodic worker safety re-arm decision (fleet-dispatcher's
# worker_rearm_should_fire), exercised through the --rearm-check hook.
#
# The re-arm now delegates to resolve_worker_class so it can never diverge from
# the actual dispatch decision: it fires iff the resolver would dispatch a
# CONCRETE class (genuinely claimable work under the full claimability gate), and
# skips on `defer` (nothing claimable) or the '' lane-default fallthrough. The
# old inline predicate excluded inflight_pr but NOT needs_gl_host, so it re-armed
# every interval on GL-host tasks a Metal pane can never claim (#1969 churn) —
# this pins that it no longer does.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
[[ -x "$DISPATCHER" ]] || { echo "test setup: fleet-dispatcher not found"; exit 1; }

PASS=0; FAIL=0
TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
assert_eq() {
    if [[ "$1" == "$2" ]]; then PASS=$((PASS+1)); echo "  ok: $3"
    else FAIL=$((FAIL+1)); echo "  FAIL: $3"; echo "      expected: $2"; echo "      actual:   $1"; fi
}

TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_SESSION="fleet-test-$$"
mkdir -p "$FLEET_STATE_DIR/projections" "$FLEET_STATE_DIR/dispatch"

write_slice() { printf '%s\n' "$2" > "$FLEET_STATE_DIR/projections/$1.json"; }
rearm() { FLEET_TEST_HOST="$1" "$DISPATCHER" --rearm-check worker; }

echo "T1: a free unblocked opus task -> rearm"
write_slice worker '{"tasks_open":[{"issue":"#10","model":"opus","owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(rearm linux)" "rearm class=opus" "claimable opus task -> rearm class=opus"

echo "T2: GL-host-only task on a Metal (mac) pane -> SKIP (the #1969 fix)"
write_slice worker '{"tasks_open":[{"issue":"#1938","model":"opus","owner":"free","blocked":false,"needs_gl_host":true}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(rearm mac)" "skip (defer=1 class=)" "gl-host task on mac -> skip (defer)"

echo "T3: same GL-host task on a Linux pane -> rearm (it IS claimable there)"
assert_eq "$(rearm linux)" "rearm class=opus" "gl-host task on linux -> rearm class=opus"

echo "T4: inflight-only task -> SKIP (no fresh-claimable work)"
write_slice worker '{"tasks_open":[{"issue":"#1640","model":"opus","owner":"free","blocked":false,"inflight_pr":{"number":1700}}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(rearm linux)" "skip (defer=1 class=)" "inflight-only -> skip (defer)"

echo "T5: empty slice -> SKIP (lane-default fallthrough must not re-arm)"
write_slice worker '{"tasks_open":[],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(rearm linux)" "skip (defer=0 class=)" "empty slice -> skip ('' fallthrough)"

echo "T6: a sonnet feedback PR -> rearm (feedback work counts now)"
write_slice worker '{"tasks_open":[],"feedback_prs":[{"number":50,"labels":["fleet:approved","fleet:has-nits"]}],"needs_plan":[]}'
assert_eq "$(rearm mac)" "rearm class=sonnet" "feedback PR -> rearm class=sonnet"

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]

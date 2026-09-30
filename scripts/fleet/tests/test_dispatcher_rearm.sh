#!/usr/bin/env bash
# Tests for the periodic worker safety re-arm decision (fleet-dispatcher's
# worker_rearm_should_fire), exercised through the --rearm-check hook.
#
# The re-arm delegates to resolve_worker_class so it can never diverge from
# the actual dispatch decision: it fires iff the resolver would dispatch a
# CONCRETE class (genuinely claimable work under the full claimability gate),
# and skips on `defer` (nothing claimable) or the '' lane-default fallthrough.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
[[ -x "$DISPATCHER" ]] || { echo "test setup: fleet-dispatcher not found"; exit 1; }

# PASS/FAIL, ok/bad and `summarize` come from the shared helper: fleet-
# positive-control parses only summarize()'s "passed: N  failed: M" line, so
# a hand-rolled "PASS: n FAIL: m" tally is unscoreable.
# shellcheck source=scripts/fleet/tests/lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
assert_eq() {
    if [[ "$1" == "$2" ]]; then ok "$3"
    else bad "$3"; echo "      expected: $2"; echo "      actual:   $1"; fi
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

echo "T7: backend-symmetric GL task on a Metal (mac) pane -> rearm (#2820)"
# The claim gate opening is not enough on its own: the dispatcher re-arms the
# worker lane by DELEGATING to resolve_worker_class, so this asserts the wake
# path opened with it. Without this case, the narrowing could land with the
# claim gate open and the re-arm still shut — the same starvation, relocated
# one layer down and invisible.
write_slice worker '{"tasks_open":[{"issue":"#2816","model":"opus","owner":"free","blocked":false,"needs_gl_host":true,"backend_symmetric":true}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(rearm mac)" "rearm class=opus" "backend-symmetric gl task on mac -> rearm class=opus"

echo "T7b: a linux-pinned task (needs_host) skips on windows, re-arms on linux"
# needs_host further restricts a GL-gated task to one host: the GL gate
# alone reads needs_gl_host as "any GL host", so a Windows pane would
# otherwise re-arm on a task that requires Linux specifically.
write_slice worker '{"tasks_open":[{"issue":"#1969","model":"sonnet","owner":"free","blocked":false,"needs_gl_host":true,"needs_host":"linux"}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(rearm windows)" "skip (defer=1 class=)" "linux-pinned task on windows -> skip (defer)"
assert_eq "$(rearm linux)" "rearm class=sonnet" "linux-pinned task on linux -> rearm class=sonnet"

echo "T7c: a standdown paces the re-arm on an unchanged slice"
# A standdown paces a re-arm on a slice that keeps electing a class: even
# once the empty-exit streak consumes the trigger, the periodic re-arm would
# re-fire the SAME slice every interval while the resolver still elects a
# class. After a standdown, the re-arm waits BASE seconds, doubling per
# consecutive standdown, until a claim clears it.
write_slice worker '{"tasks_open":[{"issue":"#10","model":"opus","owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
# `backoff <s>` within (lo, hi] — the window absorbs the second or so between
# the note and the check.
assert_backoff() {  # $1=lo $2=hi $3=label; reads $_left
    if [[ "$_left" =~ ^backoff\ ([0-9]+)$ ]] && (( BASH_REMATCH[1] > $1 && BASH_REMATCH[1] <= $2 )); then
        ok "$3 (got: $_left)"
    else
        bad "$3 (got: $_left)"
    fi
}
standdown_check() { "$DISPATCHER" --standdown-check worker --class=opus; }
export FLEET_DISPATCHER_REARM_STANDDOWN_BASE=100
assert_eq "$(rearm linux)" "rearm class=opus" "no standdown recorded -> rearm"
assert_eq "$(standdown_check)" "clear" "no standdown -> clear"
"$DISPATCHER" --note-standdown worker --class=opus
assert_eq "$(rearm linux)" "skip (standdown backoff class=opus)" \
    "one standdown -> re-arm skips inside the window"
_left=$(standdown_check)
assert_backoff 90 100 "first standdown waits ~BASE"
"$DISPATCHER" --note-standdown worker --class=opus
_left=$(standdown_check)
assert_backoff 190 200 "second consecutive standdown doubles the wait"
_left=$(FLEET_DISPATCHER_REARM_STANDDOWN_MAX=150 standdown_check)
assert_backoff 140 150 "the doubling is capped at MAX"
# The backoff is per (role, class): the sonnet lane is untouched.
assert_eq "$("$DISPATCHER" --standdown-check worker --class=sonnet)" "clear" \
    "a standdown on opus does not pace sonnet"
# BASE=0 disables the backoff outright.
assert_eq "$(FLEET_DISPATCHER_REARM_STANDDOWN_BASE=0 rearm linux)" "rearm class=opus" \
    "BASE=0 -> backoff disabled, re-arm fires"
# A productive dispatch (claimed=yes) clears it — the lane is serving work.
"$DISPATCHER" --record-outcome worker 300 --class=opus --claimed=yes
assert_eq "$(standdown_check)" "clear" "a claim clears the standdown backoff"
assert_eq "$(rearm linux)" "rearm class=opus" "…and the re-arm fires again"
# The streak reset alone (a standdown's own bookkeeping) must NOT clear the
# count, or consecutive standdowns could never escalate.
"$DISPATCHER" --note-standdown worker --class=opus
"$DISPATCHER" --record-outcome worker 300 --class=opus --claimed=no
"$DISPATCHER" --note-standdown worker --class=opus
_left=$(standdown_check)
assert_backoff 190 200 "an empty outcome between standdowns keeps the count accumulating"

echo "T7d: a changed claimable-target set bypasses the standdown delay"
"$DISPATCHER" --record-outcome worker 300 --class=opus --claimed=yes
write_slice worker '{"tasks_open":[{"issue":"#10","repo":"engine","model":"opus","owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
"$DISPATCHER" --note-standdown worker --class=opus
assert_eq "$(rearm linux)" "skip (standdown backoff class=opus)" \
    "unchanged ordered picks remain time-gated"
write_slice worker '{"tasks_open":[{"issue":"#10","repo":"engine","model":"opus","owner":"free","blocked":false}],"feedback_prs":[{"number":50,"repo":"engine","labels":["fleet:design-unblocked"]}],"needs_plan":[]}'
assert_eq "$(rearm linux)" "rearm class=opus" \
    "a newly claimable feedback target bypasses the delay"

"$DISPATCHER" --record-outcome worker 300 --class=opus --claimed=yes
mkdir -p "$FLEET_STATE_DIR/declined"
printf '2026-09-30T20:00:00Z\nreason\nworker\n' > "$FLEET_STATE_DIR/declined/task-engine-20"
write_slice worker '{"tasks_open":[{"issue":"#10","repo":"engine","model":"opus","owner":"free","blocked":false},{"issue":"#20","repo":"engine","model":"opus","owner":"free","blocked":false,"updatedAt":"2026-09-30T20:00:00Z"}],"feedback_prs":[],"needs_plan":[]}'
"$DISPATCHER" --note-standdown worker --class=opus
write_slice worker '{"tasks_open":[{"issue":"#10","repo":"engine","model":"opus","owner":"free","blocked":false},{"issue":"#20","repo":"engine","model":"opus","owner":"free","blocked":false,"updatedAt":"2026-09-30T20:01:00Z"}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(rearm linux)" "rearm class=opus" \
    "a superseded decline changes the picks and bypasses the delay"

"$DISPATCHER" --record-outcome worker 300 --class=opus --claimed=yes
write_slice worker '{"tasks_open":[{"issue":"#10","repo":"engine","model":"opus","owner":"free","blocked":false},{"issue":"#30","repo":"engine","model":"opus","owner":"pool-1","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
"$DISPATCHER" --note-standdown worker --class=opus
write_slice worker '{"tasks_open":[{"issue":"#10","repo":"engine","model":"opus","owner":"free","blocked":false},{"issue":"#30","repo":"engine","model":"opus","owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(rearm linux)" "rearm class=opus" \
    "a released claim changes the picks and bypasses the delay"

"$DISPATCHER" --record-outcome worker 300 --class=opus --claimed=yes
write_slice worker '{"tasks_open":[{"issue":"#10","repo":"engine","model":"opus","owner":"free","blocked":false}],"feedback_prs":[],"needs_plan":[]}'
"$DISPATCHER" --note-standdown worker --class=opus
rm -f "$FLEET_STATE_DIR/empty-exit-streak/worker__opus.standdown.targets"
_left=$(standdown_check)
assert_backoff 90 100 "a legacy counter without a fingerprint remains time-gated"

echo "T7e: periodic re-arm uses idle capacity instead of requiring zero activity"
"$DISPATCHER" --record-outcome worker 300 --class=opus --claimed=yes
rm -f "$FLEET_STATE_DIR/triggers/worker"
assert_eq "$(FLEET_CONCURRENCY_WORKER=2 FLEET_CAP_MODE=strict FLEET_TEST_HOST=linux \
    "$DISPATCHER" --rearm-worker-once worker 1 1 2>/dev/null)" "rearmed class=opus" \
    "one active worker with cap headroom and an idle pane re-arms"
rm -f "$FLEET_STATE_DIR/triggers/worker"
assert_eq "$(FLEET_CONCURRENCY_WORKER=2 FLEET_CAP_MODE=strict FLEET_TEST_HOST=linux \
    "$DISPATCHER" --rearm-worker-once worker 2 1 2>/dev/null)" "skip" \
    "a strict cap consumed by active workers does not re-arm"
if [[ ! -e "$FLEET_STATE_DIR/triggers/worker" ]]; then
    ok "the cap-consumed case leaves the trigger absent"
else
    bad "the cap-consumed case leaves the trigger absent"
fi
assert_eq "$(FLEET_CONCURRENCY_WORKER=2 FLEET_CAP_MODE=strict FLEET_TEST_HOST=linux \
    "$DISPATCHER" --rearm-worker-once worker 1 0 2>/dev/null)" "skip" \
    "no idle pane does not re-arm"
if [[ ! -e "$FLEET_STATE_DIR/triggers/worker" ]]; then
    ok "the no-idle-pane case leaves the trigger absent"
else
    bad "the no-idle-pane case leaves the trigger absent"
fi
assert_eq "$(FLEET_CONCURRENCY_WORKER=1 FLEET_CAP_MODE=elastic FLEET_TEST_HOST=linux \
    "$DISPATCHER" --rearm-worker-once worker 1 1 2>/dev/null)" "rearmed class=opus" \
    "elastic capacity may re-arm above the nominal cap"
rm -f "$FLEET_STATE_DIR/triggers/worker"

unset FLEET_DISPATCHER_REARM_STANDDOWN_BASE
if "$DISPATCHER" --note-standdown >/dev/null 2>&1; then
    bad "--note-standdown without a role should exit non-zero"
else
    ok "--note-standdown without a role exits non-zero"
fi
if "$DISPATCHER" --standdown-check worker --bogus >/dev/null 2>&1; then
    bad "--standdown-check with an unknown flag should exit non-zero"
else
    ok "--standdown-check with an unknown flag exits non-zero"
fi

echo "T8: the GL-only task from T2 still skips on mac (narrowing is opt-in)"
write_slice worker '{"tasks_open":[{"issue":"#1938","model":"opus","owner":"free","blocked":false,"needs_gl_host":true,"backend_symmetric":false}],"feedback_prs":[],"needs_plan":[]}'
assert_eq "$(rearm mac)" "skip (defer=1 class=)" "gl-only task on mac -> still skip"

summarize

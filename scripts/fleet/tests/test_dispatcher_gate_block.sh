#!/usr/bin/env bash
# Tests for fleet-dispatcher's dispatch-gate escalate-then-quiet pair
# (note_gate_block / clear_gate_block), driven through --gate-block-check.
#
# The gate exists so a stalled lane names WHY it stalled: before #3098 all of
# route-failed, claude-quota-closed, codex-unavailable and codex-cooldown
# folded into one "claim budget spent" line that CAP_DEFER_LOGGED then muted
# for the daemon's lifetime. These cases pin the cycle
# scripts/fleet/CLAUDE.md §"An every-tick guard that warns must
# escalate-then-quiet" prescribes:
#   - the reason reaches the operator-visible log line
#   - one line per cause, then quiet (no per-tick spam)
#   - a standing alert file only once the SAME cause holds 3 ticks
#   - the alert is rewritten every tick past the threshold, so its count=
#     tracks the live outage instead of freezing at the escalation instant
#   - a CHANGED cause re-arms the log and drops the now-false alert (the
#     misreport the whole helper was added to end)
#   - a healthy pass clears log marker, streak and alert together

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"

if [[ ! -f "$DISPATCHER" ]]; then
    echo "SKIP: fleet-dispatcher not found at $DISPATCHER" >&2
    exit 3
fi

TMPROOT=""
cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export FLEET_ALERTS_DIR="$TMPROOT/alerts"
# The dispatcher sources ~/.fleet/fleet-up.conf on startup; point it at an
# empty file so an operator's live config can't reach these cases.
: > "$TMPROOT/empty.conf"
export FLEET_CONF="$TMPROOT/empty.conf"

# note_gate_block logs via log(), which writes to stderr — fold it in.
#
# The streak is process-local but the ALERT FILE is not: it outlives the
# invocation by design (that is the whole point of a standing alert). Each case
# therefore starts from an empty alerts dir, or a prior case's escalation leaks
# in and every "alert: none" assertion reads the wrong run.
gate() {
    rm -rf "$FLEET_ALERTS_DIR"
    "$DISPATCHER" --gate-block-check "$@" 2>&1
}

echo "T1: the blocking reason reaches the log line"
out=$(gate worker codex-cooldown)
assert_contains "$out" "codex-cooldown" "T1 log names the cause"
assert_contains "$out" "claim budget" "T1 keeps the existing claim-budget wording"
assert_contains "$out" "tick 1 codex-cooldown log=yes" "T1 first tick emits a line"

echo "T2: same cause repeated goes quiet after the first line"
out=$(gate worker codex-cooldown codex-cooldown codex-cooldown)
assert_contains "$out" "tick 1 codex-cooldown log=yes" "T2 tick 1 logs"
assert_contains "$out" "tick 2 codex-cooldown log=no" "T2 tick 2 quiet"
assert_contains "$out" "tick 3 codex-cooldown log=no" "T2 tick 3 quiet"

echo "T3: no standing alert before the same cause holds 3 ticks"
out=$(gate worker codex-cooldown codex-cooldown)
assert_contains "$out" "alert: none" "T3 two ticks raise no alert file"

echo "T4: the alert lands on the 3rd consecutive identical cause"
out=$(gate worker codex-cooldown codex-cooldown codex-cooldown)
assert_contains "$out" "reason=codex-cooldown count=3" "T4 alert names cause and count"
assert_contains "$out" "role=worker" "T4 alert names the role"

echo "T5: the alert is rewritten every tick past the threshold (count tracks the outage)"
out=$(gate worker codex-cooldown codex-cooldown codex-cooldown codex-cooldown codex-cooldown)
assert_contains "$out" "count=5" "T5 alert count advances past the escalation tick"
assert_absent "$out" "count=3" "T5 alert is not frozen at the escalation instant"

echo "T6: a CHANGED cause re-arms the log — the #3098 misreport regression lock"
out=$(gate worker codex-cooldown codex-cooldown codex-cooldown claude-quota-closed)
assert_contains "$out" "tick 4 claude-quota-closed log=yes" "T6 new cause logs again"
assert_contains "$out" "claude-quota-closed" "T6 the new cause reaches the operator"

echo "T7: a changed cause drops the alert that named the cause which stopped being true"
out=$(gate worker codex-cooldown codex-cooldown codex-cooldown claude-quota-closed)
assert_absent "$out" "codex-cooldown count" "T7 stale alert is gone"
assert_contains "$out" "alert: none" "T7 no alert until the new cause re-escalates"

echo "T8: the changed cause escalates on its own 3-tick streak, not the old one's"
out=$(gate worker codex-cooldown codex-cooldown codex-cooldown \
        claude-quota-closed claude-quota-closed claude-quota-closed)
assert_contains "$out" "reason=claude-quota-closed count=3" "T8 new cause escalates from zero"

echo "T9: a healthy pass clears streak, log marker and alert together"
out=$(gate worker codex-cooldown codex-cooldown codex-cooldown - codex-cooldown)
assert_contains "$out" "tick 4 cleared" "T9 clear tick runs"
assert_contains "$out" "tick 5 codex-cooldown log=yes" "T9 log re-arms after a healthy pass"
assert_contains "$out" "alert: none" "T9 alert removed and not yet re-escalated"

echo "T10: an empty reason falls back to the generic tag rather than an empty log"
# assign_budget_hit fires with ASSIGN_BLOCK_REASON="" on the plain
# claim-attempts path, so this is a live production shape, not a hook artifact.
out=$(gate worker "")
assert_contains "$out" "claim-budget-spent" "T10 empty reason defaults"
assert_contains "$out" "tick 1 <empty> log=yes" "T10 empty reason still emits a line"

echo "T11: an empty reason is a distinct cause and re-arms against a named one"
out=$(gate worker codex-cooldown "" )
assert_contains "$out" "tick 2 <empty> log=yes" "T11 empty reason re-arms after a named one"

echo "T12: streaks are per-role — one lane's alert path is not another's"
out=$(gate worker codex-cooldown codex-cooldown codex-cooldown)
assert_contains "$out" "reason=codex-cooldown count=3" "T12 worker lane escalates"
if [[ -f "$FLEET_ALERTS_DIR/dispatch-gate-sonnet-reviewer" ]]; then
    bad "T12 per-role alert paths stay separate"
else
    ok "T12 per-role alert paths stay separate"
fi

echo "T13: the hook rejects a missing reason list (setup error, not a silent pass)"
rc=0
"$DISPATCHER" --gate-block-check worker >/dev/null 2>&1 || rc=$?
assert_eq "$rc" "2" "T13 missing reason exits 2"
rc=0
"$DISPATCHER" --gate-block-check >/dev/null 2>&1 || rc=$?
assert_eq "$rc" "2" "T13 missing role exits 2"

summarize "fleet-dispatcher gate-block tests"

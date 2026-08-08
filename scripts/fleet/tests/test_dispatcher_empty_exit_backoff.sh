#!/usr/bin/env bash
# Tests for the dispatcher's empty-exit backoff (stale/false-actionable trigger
# churn guard). A standing trigger whose role has no claimable work would
# otherwise spin the pane forever: dispatch -> worker finds nothing -> returns
# to shell in ~10s -> trigger retained -> re-dispatch every ~11s. The backoff
# counts consecutive fast exits per role and, once the count reaches
# EMPTY_STREAK_CAP, tells dispatch_role to consume the trigger instead of
# retaining it. Exercised through two inspection subcommands:
#
#   --record-outcome <role> <duration>  fold one dispatch's wall-clock seconds
#                                        into the role's streak (bump if fast,
#                                        reset if long) — the unit that
#                                        cleanup_stale_dispatches calls per
#                                        completed dispatch.
#   --empty-streak-check <role>          print `over <n>` / `under <n>` — the
#                                        retain-vs-consume decision.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"

if [[ ! -e "$DISPATCHER" ]]; then
    echo "test setup: missing $DISPATCHER" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

TMPROOT=$(mktemp -d)

# Each part runs against a fresh scratch state dir so streaks don't leak.
disp() {
    # usage: disp <state-dir> [env=val ...] -- <args...>
    local state="$1"; shift
    FLEET_STATE_DIR="$state" "$DISPATCHER" "$@"
}

echo "T1: a fast exit bumps the streak, a long run resets it"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" "fresh role starts at 0 (under cap)"
disp "$S" --record-outcome worker 5
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 1" "one fast exit (5s) -> streak 1"
disp "$S" --record-outcome worker 5
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 2" "two fast exits -> streak 2"
disp "$S" --record-outcome worker 600
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" "a long run (600s) resets the streak"

echo "T2: reaching the default cap (3) flips the decision to consume"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome worker 1
disp "$S" --record-outcome worker 1
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 2" "2 < cap(3) -> retain"
disp "$S" --record-outcome worker 1
assert_eq "$(disp "$S" --empty-streak-check worker)" "over 3" "3 >= cap(3) -> consume (stand down)"

echo "T2b: reset_empty_streak clears the streak file — post-consume reads 0"
# Simulate the reset dispatch_role applies after consuming the trigger:
# record_dispatch_outcome with a long-run duration calls reset_empty_streak
# (same function, same rm -f path), so the next check should read 0.
disp "$S" --record-outcome worker 600
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" "reset_empty_streak: streak file removed, reads 0"

echo "T3: streaks are per-role (one role's churn doesn't trip another)"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome worker 1
disp "$S" --record-outcome worker 1
disp "$S" --record-outcome worker 1
assert_eq "$(disp "$S" --empty-streak-check worker)" "over 3"  "worker tripped"
assert_eq "$(disp "$S" --empty-streak-check merger)" "under 0" "merger untouched"

echo "T4: EMPTY_STREAK_CAP is configurable"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome worker 1
assert_eq "$(FLEET_DISPATCHER_EMPTY_STREAK_CAP=1 disp "$S" --empty-streak-check worker)" \
    "over 1" "cap=1: a single fast exit stands the role down"

echo "T5: EMPTY_EXIT_SECONDS (the fast/long boundary) is configurable"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
# 600s counts as a *fast* exit when the threshold is 1000s -> bumps.
FLEET_DISPATCHER_EMPTY_EXIT_SECONDS=1000 disp "$S" --record-outcome worker 600
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 1" "600s < thr(1000s) -> bump"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
# 30s counts as a *long* exit when the threshold is 10s -> resets (no bump).
disp "$S" --record-outcome worker 5
FLEET_DISPATCHER_EMPTY_EXIT_SECONDS=10 disp "$S" --record-outcome worker 30
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" "30s >= thr(10s) -> reset"

echo "T6: a non-integer / garbage streak file is treated as 0, not a crash"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
mkdir -p "$S/empty-streak"
# Must be the QUALIFIED name (#2698): the unqualified hook reads `worker__all`,
# so seeding the bare pre-#2698 `worker` would make this assertion pass by
# reading a missing file rather than by surviving the garbage it means to test.
printf 'not-a-number\n' > "$S/empty-streak/worker__all"
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" "corrupt streak file reads as 0"

echo "T6b: a pre-#2698 unqualified streak file is inert, not adopted"
# The old bare-`<role>` files are documented as stale artifacts. A stale 99
# must not stand a healthy lane down on the first tick after the upgrade.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
mkdir -p "$S/empty-streak"
printf '99\n' > "$S/empty-streak/worker"
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" "legacy unqualified streak file ignored"

echo "T7: argument validation"
if disp "$TMPROOT" --record-outcome worker >/dev/null 2>&1; then
    FAIL=$((FAIL + 1)); echo "  FAIL: --record-outcome missing duration should exit non-zero"
else
    PASS=$((PASS + 1)); echo "  ok: --record-outcome missing duration exits non-zero"
fi
if disp "$TMPROOT" --record-outcome worker abc >/dev/null 2>&1; then
    FAIL=$((FAIL + 1)); echo "  FAIL: --record-outcome non-integer duration should exit non-zero"
else
    PASS=$((PASS + 1)); echo "  ok: --record-outcome non-integer duration exits non-zero"
fi
if disp "$TMPROOT" --empty-streak-check >/dev/null 2>&1; then
    FAIL=$((FAIL + 1)); echo "  FAIL: --empty-streak-check missing role should exit non-zero"
else
    PASS=$((PASS + 1)); echo "  ok: --empty-streak-check missing role exits non-zero"
fi

echo "T8: --claimed is authoritative; duration is not consulted (#2698)"
# The defect: a worker that walks both queues and correctly declines runs for
# minutes, so the old duration branch RESET the streak — the backoff could
# never engage for the one failure mode it was written to bound. On master the
# first assertion below reads `under 0`.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome worker 300 --claimed=no
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 1" \
    "long no-pick (300s, claimed=no) increments"
disp "$S" --record-outcome worker 300 --claimed=no
disp "$S" --record-outcome worker 300 --claimed=no
assert_eq "$(disp "$S" --empty-streak-check worker)" "over 3" \
    "three long no-picks reach the cap"
disp "$S" --record-outcome worker 300 --claimed=yes
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" \
    "a real claim resets, however long it took"
# Regression guard for the original stale-boot-trigger case: a fast exit that
# claimed nothing still increments — now because it claimed nothing.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome worker 5 --claimed=no
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 1" \
    "sub-threshold exit with no claim still increments"
# A short REAL claim resets — duration must not override an affirmative claim.
disp "$S" --record-outcome worker 5 --claimed=yes
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" \
    "sub-threshold exit that claimed resets"

echo "T9: streaks are per-(role, class) — one class cannot clear another's"
# A busy sonnet lane must not reset the streak a structurally-starved
# opus/macOS lane is accumulating.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome worker 300 --class=opus --claimed=no
disp "$S" --record-outcome worker 300 --class=opus --claimed=no
disp "$S" --record-outcome worker 300 --class=opus --claimed=no
assert_eq "$(disp "$S" --empty-streak-check worker --class=opus)" "over 3" \
    "opus lane tripped"
assert_eq "$(disp "$S" --empty-streak-check worker --class=sonnet)" "under 0" \
    "sonnet lane untouched"
# The reset is also class-scoped.
disp "$S" --record-outcome worker 300 --class=sonnet --claimed=yes
assert_eq "$(disp "$S" --empty-streak-check worker --class=opus)" "over 3" \
    "a sonnet claim does not clear the opus streak"
# ...and the unqualified counter is a third, independent one.
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" \
    "unqualified (all) counter independent of both classes"

echo "T10: the merger keeps the duration heuristic (it is outside the claim fabric)"
# AC 7, tested against what the merger actually does in production rather than
# against a hypothetical verdict. role-merger.md is explicit that the merger
# never takes a fleet-claim lock (--force-with-lease is its concurrency
# control), so it stamps no marker even on a fully successful merge run. Left
# on the marker signal it would read `empty` every time and stand its own lane
# down after EMPTY_STREAK_CAP *productive* dispatches — the same
# strictly-worse-than-today regression the plan review caught for the reviewer
# and steward lanes, one role further along.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome merger 300 --class=sonnet --claimed=no
assert_eq "$(disp "$S" --empty-streak-check merger --class=sonnet)" "under 0" \
    "a long, productive merger run resets (verdict discarded, duration wins)"
# ...and it still stands down on genuine fast idle exits, exactly as before.
disp "$S" --record-outcome merger 1 --class=sonnet --claimed=no
disp "$S" --record-outcome merger 1 --class=sonnet --claimed=no
assert_eq "$(disp "$S" --empty-streak-check merger --class=sonnet)" "under 2" "2 < cap(3) -> retain"
disp "$S" --record-outcome merger 1 --class=sonnet --claimed=no
assert_eq "$(disp "$S" --empty-streak-check merger --class=sonnet)" "over 3" \
    "merger stands down after 3 fast exits, exactly as before"
# The exemption is scoped: a claim-fabric role at the same duration increments.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome worker 300 --class=sonnet --claimed=no
assert_eq "$(disp "$S" --empty-streak-check worker --class=sonnet)" "under 1" \
    "the exemption does not leak to the worker"
# And it is overridable, so a future non-claiming role needs no code change.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
FLEET_DISPATCHER_NO_CLAIM_FABRIC_ROLES="merger triage" \
    disp "$S" --record-outcome triage 300 --claimed=no
assert_eq "$(disp "$S" --empty-streak-check triage)" "under 0" \
    "NO_CLAIM_FABRIC_ROLES is overridable"

echo "T11: every role family that can stand down is covered"
# The reviewer and steward lanes claim via `review-claim` / `steward-claim`, so
# they belong in the stamping set (#2698). If a productive reviewer dispatch
# recorded `empty`, the reviewer lane would stand down after EMPTY_STREAK_CAP
# *productive* dispatches — a strictly-worse-than-today regression. These assert
# the fold treats them like any other role once the claim signal says `yes`.
for role in sonnet-reviewer opus-reviewer epic-steward smoke-worker; do
    S=$(mktemp -d "$TMPROOT/s.XXXXXX")
    disp "$S" --record-outcome "$role" 300 --claimed=no
    disp "$S" --record-outcome "$role" 300 --claimed=no
    assert_eq "$(disp "$S" --empty-streak-check "$role")" "under 2" "$role: no-picks accumulate"
    disp "$S" --record-outcome "$role" 300 --claimed=yes
    assert_eq "$(disp "$S" --empty-streak-check "$role")" "under 0" \
        "$role: a productive claim+release dispatch resets (does NOT stand the lane down)"
done

echo "T12: the verdict derived from a dispatch record"
# The fold path itself needs tmux and a live pane; --derive-outcome exposes
# just the decision. Without it the planning and legacy arms below would ship
# on inspection alone.
#
# Probe for the hook by inspecting the subject rather than by invoking it:
# fleet-dispatcher falls through to main() on an argument it doesn't
# recognize, so a blind call against an older subject starts the DAEMON LOOP
# and hangs the run forever. That is exactly what a positive control does
# (it stages the pre-fix ref), so the guard is what makes this suite
# controllable at all. A missing hook is a genuine failure, not a skip.
if ! grep -q -- '--derive-outcome)' "$DISPATCHER"; then
    FAIL=$((FAIL + 1))
    echo "  FAIL: --derive-outcome hook absent from the subject (5 assertions not run)"
else
    S=$(mktemp -d "$TMPROOT/s.XXXXXX")
    mkdir -p "$S/dispatch-claimed"
    rec="$S/pane-9.json"
    BASE='{"role":"worker","pane":"%9","class":"opus","dispatched_at":"x","dispatched_epoch":1,"claim_marker":1}'
    PLAN='{"role":"worker","pane":"%9","class":"opus","dispatched_at":"x","dispatched_epoch":1,"claim_marker":1,"plan_issue":"engine:2698"}'
    LEGACY='{"role":"worker","pane":"%9","class":"opus","dispatched_at":"x","dispatched_epoch":1}'

    printf '%s\n' "$BASE" > "$rec"
    rm -f "$S/dispatch-claimed/pane-9"
    assert_eq "$(disp "$S" --derive-outcome "$rec" '%9')" "no" \
        "no marker on disk -> empty (the diligent no-pick)"
    : > "$S/dispatch-claimed/pane-9"
    assert_eq "$(disp "$S" --derive-outcome "$rec" '%9')" "yes" \
        "marker present -> claimed"
    # The pane key is derived from the pane id, so another pane's marker must
    # not be read as this one's.
    rm -f "$S/dispatch-claimed/pane-9"; : > "$S/dispatch-claimed/pane-8"
    assert_eq "$(disp "$S" --derive-outcome "$rec" '%9')" "no" \
        "a different pane's marker is not borrowed"
    # A planning assignment is productive even though the pane never stamped:
    # the dispatcher took the planning-claim pre-launch (#2197).
    printf '%s\n' "$PLAN" > "$rec"
    rm -f "$S/dispatch-claimed/pane-9"
    assert_eq "$(disp "$S" --derive-outcome "$rec" '%9')" "yes" \
        "plan_issue with no marker -> claimed (pre-claimed planning assignment)"
    # A record from before this change has no claim_marker; its pane's wrapper
    # never exported the flag, so the verdict is unknown, not empty.
    printf '%s\n' "$LEGACY" > "$rec"
    rm -f "$S/dispatch-claimed/pane-9"
    assert_eq "$(disp "$S" --derive-outcome "$rec" '%9')" "" \
        "legacy record -> unknown (falls back to the duration heuristic)"
fi
# Unknown really does route to duration, not to a silent increment. Outside the
# hook guard: it runs through --record-outcome, which every version has.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome worker 300 --class=opus
assert_eq "$(disp "$S" --empty-streak-check worker --class=opus)" "under 0" \
    "unknown + long duration resets, exactly as before this change"

echo "T13: --claimed / --class argument validation"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
if disp "$S" --record-outcome worker 300 --claimed=maybe >/dev/null 2>&1; then
    FAIL=$((FAIL + 1)); echo "  FAIL: --claimed=maybe should exit non-zero"
else
    PASS=$((PASS + 1)); echo "  ok: --claimed with a non-yes/no value exits non-zero"
fi
if disp "$S" --record-outcome worker 300 --bogus >/dev/null 2>&1; then
    FAIL=$((FAIL + 1)); echo "  FAIL: unknown --record-outcome argument should exit non-zero"
else
    PASS=$((PASS + 1)); echo "  ok: unknown --record-outcome argument exits non-zero"
fi
if disp "$S" --empty-streak-check worker --bogus >/dev/null 2>&1; then
    FAIL=$((FAIL + 1)); echo "  FAIL: unknown --empty-streak-check argument should exit non-zero"
else
    PASS=$((PASS + 1)); echo "  ok: unknown --empty-streak-check argument exits non-zero"
fi
# An empty `--claimed=` must not be read as an affirmative claim; it falls back
# to the duration heuristic like an omitted flag (dual-spelling arm discipline).
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
disp "$S" --record-outcome worker 300 --claimed=
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" \
    "--claimed= (empty) falls back to the duration heuristic"

echo
echo "passed: $PASS  failed: $FAIL"
[[ "$FAIL" -eq 0 ]]

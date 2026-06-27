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
printf 'not-a-number\n' > "$S/empty-streak/worker"
assert_eq "$(disp "$S" --empty-streak-check worker)" "under 0" "corrupt streak file reads as 0"

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

echo
echo "passed: $PASS  failed: $FAIL"
[[ "$FAIL" -eq 0 ]]

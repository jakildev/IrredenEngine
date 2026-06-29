#!/usr/bin/env bash
# Tests for the claimable-count fan-out cap's racing-worker counter,
# fleet-dispatcher's count_recent_active_for_class (exposed via the
# --count-recent-active-class hook).
#
# The dispatcher caps its idle-pane fan-out at one worker per claimable item of
# the elected class. The headroom is `claimable - racing`, where `racing` is the
# count of in-flight dispatches of that class young enough to still be competing
# to claim (dispatch record younger than the settle window). A worker past the
# window has settled onto claimed work and must NOT count — that's what keeps
# the cap from serializing independent tasks. This file pins that age-window
# behaviour:
#   - no records -> 0
#   - a just-dispatched worker counts (age < window)
#   - a settled worker does NOT count (age >= window)
#   - per-class isolation (opus racing count ignores sonnet/fable records)
#   - a record missing dispatched_epoch is skipped (defensive, not crash)

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

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_SESSION="fleet-test-$$"
DISPATCH_DIR="$FLEET_STATE_DIR/dispatch"
mkdir -p "$DISPATCH_DIR"

NOW=$(date +%s)

# Write a dispatch record for a pane with a given class and age (seconds ago).
write_record() {
    local pane="$1" class="$2" age="$3" epoch
    epoch=$(( NOW - age ))
    printf '{"role":"worker","pane":"%%%s","class":"%s","dispatched_at":"x","dispatched_epoch":%s}\n' \
        "$pane" "$class" "$epoch" > "$DISPATCH_DIR/pane-$pane.json"
}

recent() {
    "$DISPATCHER" --count-recent-active-class "$1" "$2"
}

# --- T1: no records -> 0 -----------------------------------------------------
echo "T1: empty dispatch dir"
assert_eq "$(recent opus 90)" "0" "no records -> 0 racing"

# --- T2: a just-dispatched worker counts -------------------------------------
echo "T2: fresh dispatch inside the window counts"
write_record 0 opus 5
assert_eq "$(recent opus 90)" "1" "age 5s < 90s window -> counts as racing"

# --- T3: a settled worker (past window) does NOT count -----------------------
echo "T3: settled dispatch past the window is excluded"
write_record 1 opus 300
assert_eq "$(recent opus 90)" "1" "age 300s >= 90s window -> excluded; only the fresh one counts"
# At the boundary the older one is still out (300 != <90), proven above. Now a
# wider window should sweep it back in:
assert_eq "$(recent opus 600)" "2" "widen window to 600s -> both opus records count"

# --- T4: per-class isolation -------------------------------------------------
echo "T4: racing count is per class"
write_record 2 sonnet 5
write_record 3 fable 5
assert_eq "$(recent opus 90)" "1" "sonnet/fable young records do not inflate opus racing"
assert_eq "$(recent sonnet 90)" "1" "sonnet racing counted independently"
assert_eq "$(recent fable 90)" "1" "fable racing counted independently"
assert_eq "$(recent opus 0)" "0" "zero window -> nothing is 'recent'"

# --- T5: malformed record (no dispatched_epoch) is skipped, not fatal --------
echo "T5: record missing dispatched_epoch is skipped"
printf '{"role":"worker","pane":"%%8","class":"opus","dispatched_at":"x"}\n' \
    > "$DISPATCH_DIR/pane-8.json"
assert_eq "$(recent opus 90)" "1" "epoch-less opus record skipped; fresh one still counts"

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]

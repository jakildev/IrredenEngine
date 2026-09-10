#!/usr/bin/env bash
# Tests for fleet-babysit's interruptible_sleep — the between-iteration wait
# that a shutdown sentinel or a fresh scout trigger can cut short.
#
# The function takes no wake-suppression floor: a trigger that is present wakes
# the sleep on the next poll, unconditionally. T4 locks that shape, because the
# floor it replaced was inert (see #2831) and so left no behavioural trace for
# T1-T3 to catch.
#
# Covers:
#   - a standing trigger wakes the sleep immediately (rc=2) — the behavior the
#     dead floor was the only thing that could ever have suppressed
#   - the shutdown sentinel still short-circuits (rc=1)
#   - an uninterrupted sleep still elapses and returns 0
#   - the min_floor parameter is gone and every call site passes an arity the
#     function accepts (1 or 2 args) — the regression lock for the deletion,
#     and the one arm here that is RED at the pre-#2831 tree
#
# The function is sed-extracted from the shipped fleet-babysit rather than
# re-pasted, so the arms exercise the text that actually ships. It is
# self-contained: two globals ($SHUTDOWN_FLAG, $HEARTBEAT_FILE) and its args.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
BABYSIT="$SCRIPT_DIR/fleet-babysit"
[[ -x "$BABYSIT" ]] || { echo "test setup: fleet-babysit not found at $BABYSIT" >&2; exit 1; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

extract_fn() {  # extract_fn <name> — print the function body from fleet-babysit
    sed -n "/^$1() {/,/^}/p" "$BABYSIT"
}
eval "$(extract_fn interruptible_sleep)"
[[ "$(type -t interruptible_sleep)" == "function" ]] \
    || { echo "test setup: interruptible_sleep not extracted from $BABYSIT" >&2; exit 1; }

# The two globals the function reads. Sandboxed so the suite can never see or
# touch the live fleet's shutdown flag.
SHUTDOWN_FLAG="$TMPROOT/shutdown-in-progress"
HEARTBEAT_FILE="$TMPROOT/heartbeat"
TRIGGER="$TMPROOT/trigger"

# run_sleep <duration> [trigger] -> prints "<rc> <elapsed_seconds>"
run_sleep() {
    local start rc=0 end
    start=$(date +%s)
    if [[ $# -ge 2 ]]; then
        interruptible_sleep "$1" "$2" || rc=$?
    else
        interruptible_sleep "$1" || rc=$?
    fi
    end=$(date +%s)
    echo "$rc $(( end - start ))"
}

# --- T1: a standing trigger wakes a nominally-long sleep immediately --------
# 30s nominal; a trigger that already exists must cut it on the first poll.
# This is exactly what min_floor=1800 was built to suppress, so it is the
# behavior statement the deletion makes true unconditionally.
echo "T1: standing trigger wakes the sleep immediately"
rm -f "$SHUTDOWN_FLAG"; : > "$TRIGGER"
read -r rc elapsed <<< "$(run_sleep 30 "$TRIGGER")"
assert_eq "$rc" "2" "standing trigger returns rc=2 (early wake)"
[[ "$elapsed" -lt 10 ]] \
    && ok "early wake took ${elapsed}s (< 10s of a 30s nominal sleep)" \
    || bad "early wake took ${elapsed}s — expected < 10s of a 30s nominal sleep"

# --- T2: the shutdown sentinel still short-circuits -------------------------
echo "T2: shutdown sentinel short-circuits the sleep"
: > "$SHUTDOWN_FLAG"; rm -f "$TRIGGER"
read -r rc elapsed <<< "$(run_sleep 30)"
assert_eq "$rc" "1" "shutdown sentinel returns rc=1"
[[ "$elapsed" -lt 10 ]] \
    && ok "sentinel exit took ${elapsed}s (< 10s of a 30s nominal sleep)" \
    || bad "sentinel exit took ${elapsed}s — expected < 10s of a 30s nominal sleep"

# --- T3: an uninterrupted sleep elapses and returns 0 -----------------------
echo "T3: uninterrupted sleep elapses normally"
rm -f "$SHUTDOWN_FLAG" "$TRIGGER"
read -r rc elapsed <<< "$(run_sleep 1)"
assert_eq "$rc" "0" "uninterrupted sleep returns rc=0"
[[ "$elapsed" -lt 10 ]] \
    && ok "1s sleep elapsed in ${elapsed}s" \
    || bad "1s sleep took ${elapsed}s — expected it to elapse promptly"

# --- T4: min_floor is gone and every call site matches the surviving arity ---
# The deletion's regression lock. T1-T3 are behavior statements that hold on
# both sides of #2831 (min_floor defaulted to 0, so a 2-arg call always woke);
# this arm is the differential — it is RED at the pre-#2831 tree.
echo "T4: min_floor is gone and all call sites pass 1 or 2 args"
BODY=$(extract_fn interruptible_sleep)
assert_absent "$BODY" "min_floor" "the shipped function body has no min_floor parameter"
assert_eq "$(grep -c 'FLEET_MIN_BACKOFF\|MIN_BACKOFF_SECONDS' "$BABYSIT" || true)" "0" \
    "fleet-babysit no longer names FLEET_MIN_BACKOFF / MIN_BACKOFF_SECONDS"
# Positive control for the line above: the same grep form, against a name that
# IS still in the file, must be non-zero. Without this, a typo'd pattern or an
# unreadable path would score the deletion as done.
[[ "$(grep -c 'LONG_BACKOFF_SECONDS' "$BABYSIT" || true)" -gt 0 ]] \
    && ok "grep control: the same form still finds LONG_BACKOFF_SECONDS" \
    || bad "grep control failed — the zero-hit assertion above is vacuous"

# Count the "..."-quoted arguments on each call line (the definition line has
# no space before '(' so quoting the trailing space excludes it).
call_args() {
    local line="$1"
    line="${line#*interruptible_sleep }"
    line="${line%%||*}"
    printf '%s\n' "$line" | grep -o '"[^"]*"' | wc -l | tr -d ' '
}
CALL_LINES=$(grep -n 'interruptible_sleep "' "$BABYSIT")
CALL_COUNT=$(printf '%s\n' "$CALL_LINES" | grep -c . || true)
# Positive control on the scan itself: a zero-call-site sweep would pass the
# loop below vacuously, so assert the call sites were actually found.
[[ "$CALL_COUNT" -ge 4 ]] \
    && ok "found $CALL_COUNT interruptible_sleep call sites to check" \
    || bad "found only $CALL_COUNT call sites — expected >= 4; the arity scan would be vacuous"
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    lineno="${line%%:*}"
    n=$(call_args "$line")
    if [[ "$n" -ge 1 && "$n" -le 2 ]]; then
        ok "call site :$lineno passes $n arg(s) — within the surviving arity"
    else
        bad "call site :$lineno passes $n args — interruptible_sleep accepts 1 or 2"
        echo "        line: $line"
    fi
done <<< "$CALL_LINES"

summarize "fleet-babysit interruptible_sleep tests"

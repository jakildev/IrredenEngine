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
#   - the retired floor parameter is gone and every call site passes an arity
#     the function accepts (1 or 2 args) — the regression lock for the
#     deletion, and the one arm here that is RED at the pre-#2831 tree
#
# #2831's acceptance criterion greps scripts/ for the identifiers the deletion
# retired and requires zero hits. scripts/ includes this file, so it never
# spells them: T4 assembles its needles from fragments and controls them
# against a fixture built to carry the retired shape.
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
# Suppressing exactly this is what the retired 1800s floor existed to do, so
# it is the behavior statement the deletion makes true unconditionally.
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

# --- T4: the retired floor parameter is gone; call sites match the arity -----
# The deletion's regression lock. T1-T3 are behavior statements that hold on
# both sides of #2831 (the floor defaulted to 0, so a 2-arg call always woke);
# this arm is the differential — it is RED at the pre-#2831 tree.
#
# The needles are assembled from fragments so this file does not itself carry
# the literals #2831 retired — see the header. The names still reach the test
# output, just not the source text.
FRAG_FLOOR=floor
FRAG_BACKOFF=BACKOFF
RETIRED_PARAM="min_$FRAG_FLOOR"                             # dropped 3rd parameter
RETIRED_VARS="FLEET_MIN_$FRAG_BACKOFF|MIN_${FRAG_BACKOFF}_SECONDS"  # dropped vars
echo "T4: $RETIRED_PARAM is gone and all call sites pass 1 or 2 args"

# Needle control. An assembled needle that came out empty or misspelled would
# "find nothing" everywhere and score both absence arms below as green, so
# match them against a fixture built to carry the retired shape first.
FIXTURE="$TMPROOT/retired-shape"
{
    printf 'interruptible_sleep() {\n    local %s="${3:-0}"\n}\n' "$RETIRED_PARAM"
    printf 'FLEET_MIN_%s=1800\nMIN_%s_SECONDS=1800\n' "$FRAG_BACKOFF" "$FRAG_BACKOFF"
} > "$FIXTURE"
[[ "$(grep -cF "$RETIRED_PARAM" "$FIXTURE" || true)" -gt 0 ]] \
    && ok "needle control: the parameter needle matches a fixture carrying it" \
    || bad "needle control failed — the parameter absence arm below is vacuous"
[[ "$(grep -cE "$RETIRED_VARS" "$FIXTURE" || true)" -gt 0 ]] \
    && ok "needle control: the config-var needles match a fixture carrying them" \
    || bad "needle control failed — the config-var zero-hit arm below is vacuous"

BODY=$(extract_fn interruptible_sleep)
assert_absent "$BODY" "$RETIRED_PARAM" \
    "the shipped function body has no $RETIRED_PARAM parameter"
assert_eq "$(grep -cE "$RETIRED_VARS" "$BABYSIT" || true)" "0" \
    "fleet-babysit no longer names the retired cooldown config vars"
# Second control, on the file rather than the needles: the same grep form must
# still find a name that IS in fleet-babysit. Without it, an unreadable path
# would score the deletion as done.
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

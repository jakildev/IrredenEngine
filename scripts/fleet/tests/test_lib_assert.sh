#!/usr/bin/env bash
# Tests for lib_assert.sh's assert_contains / assert_absent — specifically
# that their verdict is grep's, never a writer's SIGPIPE.
#
# `grep -q` stops reading at its first match. A haystack piped in from the left
# therefore leaves its writer pushing into a closed pipe, and under the
# `set -o pipefail` every suite here sets, that 141 becomes the pipeline's
# status: a present needle reads as a miss in assert_contains, and as ABSENT in
# assert_absent. The second direction is the one that has to be locked — it
# turns a negative assertion green while the thing it forbids is present, in
# the helper every bash suite here sources (#3205).
#
# The window opens on haystack size AND match position together, so the arms
# below put the needle on line 1 and grow the haystack until the piped form
# demonstrably breaks on THIS host. Growing rather than fixing a size is what
# keeps the lock from going vacuous on a host whose pipe capacity exceeds any
# single number this file could name.
#
# Covers:
#   - assert_contains finds a needle past the early-close point
#   - assert_absent does NOT call a present needle absent (the silent direction)
#   - control: the piped form really does close early on this haystack
#   - controls: a genuinely absent needle still reads as absent, both ways

set -euo pipefail

source "$(dirname "$0")/lib_assert.sh"

NEEDLE="IR-ASSERT-NEEDLE"

# The piped form — the shape whose SIGPIPE this suite exists to forbid — kept
# verbatim so the control measures that shape rather than a paraphrase of it.
piped_form_has() {  # piped_form_has <haystack> <needle>
    printf '%s' "$1" | grep -qF -- "$2"
}

# Grow the haystack until the piped form fails on it. Starting over the 64 KiB
# default capacity and doubling covers a host whose pipe capacity was raised,
# without asserting any particular capacity.
build_haystack() {
    local kib=256 hay rc
    while (( kib <= 8192 )); do
        # Few, long filler lines rather than many short ones: the needle is on
        # line 1 either way, but this keeps a failing arm's haystack dump to a
        # few dozen lines instead of thousands.
        hay="$NEEDLE"$'\n'"$(head -c $((kib * 1024)) /dev/zero | tr '\0' 'x' | fold -w 4000)"
        set +e
        ( piped_form_has "$hay" "$NEEDLE" )
        rc=$?
        set -e
        if [[ "$rc" -ne 0 ]]; then
            HAYSTACK="$hay"
            HAYSTACK_KIB="$kib"
            PIPED_RC="$rc"
            return 0
        fi
        kib=$(( kib * 2 ))
    done
    return 1
}

if ! build_haystack; then
    echo "SKIP: no haystack up to 8 MiB makes the piped form close early —" >&2
    echo "      this host cannot exhibit the regression, so the arms would be vacuous" >&2
    exit 3
fi

# --- T1: the control — the regression is reproducible here -----------------
# Asserted, not assumed: if this goes green the haystack fits this host's pipe
# buffer, T2/T3 stop proving anything, and the suite must say so.
echo "T1: control — the piped form breaks on a ${HAYSTACK_KIB} KiB haystack"
assert_eq "$PIPED_RC" "141" "the piped form reports the writer's SIGPIPE, not grep's match"

# --- T2: assert_contains reports the match ---------------------------------
echo "T2: assert_contains finds a needle whose match precedes the early close"
assert_contains "$HAYSTACK" "$NEEDLE" "a present needle in a large haystack is found"

# --- T3: assert_absent does not call a present needle absent ---------------
# The silent direction. Driven through the real assert_absent and read off its
# own output, because what is under test is the verdict it reports, not the
# predicate alone. Running it in a command substitution keeps its PASS/FAIL
# bumps out of this suite's tally.
echo "T3: assert_absent rejects a present needle rather than passing it"
absent_verdict=$( PASS=0; FAIL=0; assert_absent "$HAYSTACK" "$NEEDLE" "probe" 2>&1 || true )
case "$absent_verdict" in
    *"FAIL: probe"*) verdict=rejected ;;
    *"ok: probe"*)   verdict=passed-silently ;;
    *)               verdict=unrecognized ;;
esac
assert_eq "$verdict" "rejected" "assert_absent fails when the needle is in fact present"

# --- T4: controls — a genuinely absent needle still reads as absent --------
# Keeps T2/T3 from passing for the trivial reason that the helper says "found"
# unconditionally.
echo "T4: control — an absent needle is still reported absent both ways"
assert_absent "$HAYSTACK" "IR-ASSERT-NEEDLE-THAT-IS-NOT-THERE" \
    "a needle that is not in the haystack is absent"
contains_verdict=$( PASS=0; FAIL=0; assert_contains "$NEEDLE" "IR-ASSERT-NEEDLE-THAT-IS-NOT-THERE" "probe" 2>&1 || true )
case "$contains_verdict" in
    *"FAIL: probe"*) verdict=rejected ;;
    *"ok: probe"*)   verdict=passed-silently ;;
    *)               verdict=unrecognized ;;
esac
assert_eq "$verdict" "rejected" "assert_contains fails when the needle is genuinely missing"

summarize "lib_assert assertion tests"

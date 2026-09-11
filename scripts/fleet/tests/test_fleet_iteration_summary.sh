#!/usr/bin/env bash
# Tests for fleet-iteration-summary's confirmation output. See #3115.
#
#   T1: successful invocation prints "recorded <path>" on stdout, exits 0,
#       and the named path is a real file (byte-level check, not a grep for
#       the word "recorded") whose contents match the given summary
#   T2: the dropped path (mkdir -p can't create the summaries dir) prints a
#       distinct "dropped" line to stderr, exits nonzero, and writes no file
#   T3: too-few-args usage message is unchanged (regression pin)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$(dirname "$0")/lib_preflight.sh"
TOOL="$SCRIPT_DIR/fleet-iteration-summary"

if [[ ! -x "$TOOL" ]]; then
    echo "test setup: fleet-iteration-summary not executable at $TOOL" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""

cleanup() {
    if [[ -n "$TMPROOT" && -d "$TMPROOT" ]]; then
        rm -rf "$TMPROOT"
    fi
}
trap cleanup EXIT

TMPROOT=$(mktemp -d)

# --- T1: success prints the recorded path; the path is a real file ---------
echo "T1: successful invocation names an existing file"
SUMMARIES_DIR="$TMPROOT/t1"
set +e
out=$(FLEET_SUMMARIES_DIR="$SUMMARIES_DIR" "$TOOL" pool-9 "did the thing" 2>&1)
rc=$?
set -e
assert_eq "$rc" "0" "T1 exits 0"
assert_contains "$out" "fleet-iteration-summary: recorded " "T1 prints a recorded line"

# Pull the path out of the confirmation line and check it exists on disk —
# a grep for the word "recorded" would pass even if the file was never
# written, which is exactly the gap acceptance criterion 2 closes.
recorded_path="${out#fleet-iteration-summary: recorded }"
if [[ -f "$recorded_path" ]]; then
    ok "T1 recorded path is a real file"
else
    bad "T1 recorded path is a real file"
    echo "        path: $recorded_path"
fi
assert_eq "$(cat "$recorded_path" 2>/dev/null || echo '<missing>')" "did the thing" \
    "T1 file content matches the given summary"

# --- T2: the dropped path is distinct and writes no file -------------------
echo "T2: a write failure prints a distinct dropped line, no file written"
BLOCKER="$TMPROOT/blocker"
touch "$BLOCKER"
# mkdir -p fails here because $BLOCKER is a regular file, not a directory,
# so it can never be descended into.
BAD_SUMMARIES_DIR="$BLOCKER/subdir"
set +e
out=$(FLEET_SUMMARIES_DIR="$BAD_SUMMARIES_DIR" "$TOOL" pool-9 "will not land" 2>&1)
rc=$?
set -e
if [[ "$rc" -eq 0 ]]; then
    bad "T2 exits nonzero on a dropped write"
else
    ok "T2 exits nonzero on a dropped write"
fi
assert_contains "$out" "fleet-iteration-summary: dropped" "T2 prints a distinct dropped line"
assert_absent "$out" "fleet-iteration-summary: recorded" "T2 does not also claim success"
if [[ -d "$BAD_SUMMARIES_DIR" ]]; then
    bad "T2 must not create the (unreachable) summaries dir"
else
    ok "T2 creates no summary file"
fi

# --- T3: too-few-args usage is unchanged (regression pin) -------------------
echo "T3: too-few-args still prints usage to stderr, nonzero exit"
set +e
out=$(FLEET_SUMMARIES_DIR="$TMPROOT/t3" "$TOOL" pool-9 2>&1)
rc=$?
set -e
if [[ "$rc" -eq 0 ]]; then
    bad "T3 exits nonzero with too few args"
else
    ok "T3 exits nonzero with too few args"
fi
assert_contains "$out" "Usage: fleet-iteration-summary" "T3 prints the usage line"

summarize "fleet-iteration-summary tests"

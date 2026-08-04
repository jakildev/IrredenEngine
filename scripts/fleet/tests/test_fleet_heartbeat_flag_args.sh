#!/usr/bin/env bash
# Tests for fleet-heartbeat's flag-arg handling (#2784).
#
# The role-name validator's regex allowed a leading '-', so any flag-shaped
# argument (--help, -h, --once, ...) fell through to `touch
# "$heartbeats_dir/$role"` instead of being rejected — --help in particular
# silently created a `--help` file instead of printing usage. Fixed by
# adding an explicit -h|--help case ahead of the validator and tightening
# the regex to reject a leading dash.
#
#   T1: --help prints the usage block, exits 0, creates no heartbeat file
#   T2: -h same as --help
#   T3: --help's printed usage block does not leak any post-header code
#       line (guards the hardcoded `sed -n 'N,Mp'` range per
#       scripts/fleet/CLAUDE.md's --help-drift rule, #2433)
#   T4: --bogus (an unrecognized flag) is rejected with the invalid-role
#       diagnostic, rc=2, no file created
#   T5: a normal role name still touches its heartbeat file, rc=0
#       (regression pin — the fix must not touch the working path)
#   T6: no-arg invocation is unchanged (rc=2, usage to stderr)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
HEARTBEAT="$SCRIPT_DIR/fleet-heartbeat"

if [[ ! -x "$HEARTBEAT" ]]; then
    echo "test setup: fleet-heartbeat not executable at $HEARTBEAT" >&2
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
export HOME="$TMPROOT"

# --- T1: --help prints usage, exits 0, no file ------------------------------
echo "T1: --help prints usage block, exits 0, creates no heartbeat file"
set +e
out=$(HOME="$TMPROOT" "$HEARTBEAT" --help 2>&1)
rc=$?
set -e
assert_eq "$rc" "0" "--help exits 0"
assert_contains "$out" "fleet-heartbeat <role-name>" "--help output names the usage form"
if [[ -e "$TMPROOT/.fleet/heartbeats/--help" ]]; then
    bad "--help must not create a heartbeat file"
else
    ok "--help creates no heartbeat file"
fi

# --- T2: -h behaves like --help ---------------------------------------------
echo "T2: -h prints usage block, exits 0, creates no heartbeat file"
set +e
out=$(HOME="$TMPROOT" "$HEARTBEAT" -h 2>&1)
rc=$?
set -e
assert_eq "$rc" "0" "-h exits 0"
assert_contains "$out" "fleet-heartbeat <role-name>" "-h output names the usage form"
if [[ -e "$TMPROOT/.fleet/heartbeats/-h" ]]; then
    bad "-h must not create a heartbeat file"
else
    ok "-h creates no heartbeat file"
fi

# --- T3: printed usage block does not leak code lines -----------------------
echo "T3: --help output does not leak post-header code lines (#2433 shape)"
help_out=$(HOME="$TMPROOT" "$HEARTBEAT" --help 2>&1)
assert_absent "$help_out" "set -euo pipefail" "--help does not leak the set -e line"
assert_absent "$help_out" "heartbeats_dir=" "--help does not leak script body"

# --- T4: an unrecognized flag is rejected, not silently accepted -----------
echo "T4: --bogus is rejected as an invalid role name"
set +e
out=$(HOME="$TMPROOT" "$HEARTBEAT" --bogus 2>&1)
rc=$?
set -e
assert_eq "$rc" "2" "--bogus exits 2"
assert_contains "$out" "invalid role name" "--bogus gets the invalid-role diagnostic"
if [[ -e "$TMPROOT/.fleet/heartbeats/--bogus" ]]; then
    bad "--bogus must not create a heartbeat file"
else
    ok "--bogus creates no heartbeat file"
fi

# --- T5: a normal role name still works (regression pin) -------------------
echo "T5: a normal role name still touches its heartbeat file"
set +e
out=$(HOME="$TMPROOT" "$HEARTBEAT" pool-2 2>&1)
rc=$?
set -e
assert_eq "$rc" "0" "pool-2 exits 0"
if [[ -e "$TMPROOT/.fleet/heartbeats/pool-2" ]]; then
    ok "pool-2 creates its heartbeat file"
else
    bad "pool-2 must create its heartbeat file"
fi

# --- T6: no-arg invocation is unchanged --------------------------------------
echo "T6: no-arg invocation still prints usage to stderr, exits 2"
set +e
out=$(HOME="$TMPROOT" "$HEARTBEAT" 2>&1)
rc=$?
set -e
assert_eq "$rc" "2" "no-arg exits 2"
assert_contains "$out" "usage: fleet-heartbeat <role-name>" "no-arg prints usage"

summarize "fleet-heartbeat flag-arg tests"

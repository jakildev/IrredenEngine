# Shared assertion helpers for the bash tests in this directory. Sourced,
# never executed — the name deliberately avoids the test_*.sh pattern so
# anything that globs for tests skips it:
#
#     source "$(dirname "$0")/lib_assert.sh"
#
# Provides the PASS/FAIL counters, ok/bad bumpers, the assert_* family, and
# summarize. End every test with summarize (optionally with a suite label)
# as the last command so the script's exit status reflects the failure
# count:
#
#     summarize "fleet-foo tests"
#
# assert_contains / assert_absent match the needle as a fixed string
# (grep -F), one line at a time — a needle spanning a newline never
# matches. Tests that need path-existence or exit-code assertions define
# those locally (see test_fleet_claim_safety_guards.sh).
#
# Sourcing this file also pulls in lib_preflight.sh (the mis-staged-control
# guard). A suite that resolves a fleet-* wrapper before it gets here sources
# lib_preflight.sh directly as well — see that file's header.

PASS=0
FAIL=0

ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

# The mis-staged-control preflight (require_fleet_lib_dir + its auto-fire) lives
# in lib_preflight.sh, sourced here so every suite that sources this file gets
# the guard with no edit and with no dependency on whether SCRIPT_DIR was
# assigned first (#2845). Deriving the directory from BASH_SOURCE rather than
# $0 is what makes that hold when this file is sourced from a driver script
# elsewhere.
#
# A stage carrying lib_assert.sh without lib_preflight.sh beside it is itself
# partial, so failing here with the setup-shaped rc=2 is the correct reading —
# a bare "file not found" followed by undefined-function errors is not.
_ir_assert_dir=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd) || _ir_assert_dir=""
if [[ ! -f "$_ir_assert_dir/lib_preflight.sh" ]]; then
    echo "test setup: lib_preflight.sh missing beside lib_assert.sh at $_ir_assert_dir" >&2
    echo "            — the stage is partial. Stage the whole directory:" >&2
    echo "              git archive <ref> scripts/fleet | tar -x -C <tmpdir>" >&2
    echo "            or just use: fleet-positive-control <test-file> <ref>" >&2
    exit 2
fi
# shellcheck source=lib_preflight.sh
source "$_ir_assert_dir/lib_preflight.sh"
unset _ir_assert_dir

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        ok "$msg"
    else
        bad "$msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

assert_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if printf '%s' "$haystack" | grep -qF -- "$needle"; then
        ok "$msg"
    else
        bad "$msg"
        echo "        expected to find: $needle"
        echo "        in:"; printf '%s\n' "$haystack" | sed 's/^/          | /'
    fi
}

assert_absent() {
    local haystack="$1" needle="$2" msg="$3"
    if printf '%s' "$haystack" | grep -qF -- "$needle"; then
        bad "$msg"
        echo "        did NOT expect: $needle"
        echo "        in:"; printf '%s\n' "$haystack" | sed 's/^/          | /'
    else
        ok "$msg"
    fi
}

summarize() {
    echo ""
    if [[ $# -gt 0 ]]; then
        echo "$1: $PASS passed, $FAIL failed"
    else
        echo "passed: $PASS  failed: $FAIL"
    fi
    [[ "$FAIL" -eq 0 ]]
}

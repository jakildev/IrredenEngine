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
# (grep -F), one line at a time: the needle has to fall within a single
# haystack line. A needle that itself spans a newline is not matched as a
# contiguous span — grep -F reads it as a newline-separated pattern LIST, so
# it reports a match when any one of its lines matches. Pass single-line
# needles. Tests that need path-existence or exit-code assertions define
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

# Does <haystack> contain <needle> on some single line? Backs both assert_*
# below.
#
# The haystack is handed to grep as a process substitution rather than piped
# in, and that is load-bearing. `grep -q` stops reading at its first match; with
# the haystack on the left of a pipe that closes the pipe while printf still has
# bytes to write, printf dies of SIGPIPE (141), and under `set -o pipefail` —
# which every suite here sets — 141 becomes the whole pipeline's status. A
# present needle then reads as a miss in assert_contains and, in the direction
# that fails silently, as absent in assert_absent. Keeping the writer off the
# pipeline means only grep's own status is ever observed.
#
# The window is "first match lands before the writer drains", so it opens on
# haystack size and match position together, not on either alone: a 51 KB
# haystack whose first match is at byte 19 K is enough, and the same haystack
# matching near its end is not (#3205).
#
# The writer still meets a closed pipe when grep stops early — that is now
# harmless to the verdict, but bash announces it on stderr ("printf: write
# error: Broken pipe") on the platforms whose printf builtin reports EPIPE
# rather than dying of SIGPIPE. Discard the writer's stderr so a passing
# assertion stays silent; printf writing a string to a pipe has no other
# failure worth surfacing.
_ir_haystack_has() {  # _ir_haystack_has <haystack> <needle>
    grep -qF -- "$2" <(printf '%s' "$1" 2>/dev/null)
}

assert_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if _ir_haystack_has "$haystack" "$needle"; then
        ok "$msg"
    else
        bad "$msg"
        echo "        expected to find: $needle"
        echo "        in:"; printf '%s\n' "$haystack" | sed 's/^/          | /'
    fi
}

assert_absent() {
    local haystack="$1" needle="$2" msg="$3"
    if _ir_haystack_has "$haystack" "$needle"; then
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

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

PASS=0
FAIL=0

ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

# require_fleet_lib_dir <dir> — abort the suite when <dir> is a partially
# staged scripts/fleet/ tree, instead of letting it score as assertion
# failures.
#
# The shell wrappers dispatch to the fleet_*.py modules beside them, so a
# stage holding only the script under test plus this file aborts every
# invocation on its own lib-dir preflight with rc=1 and empty stdout. Absent
# this guard the suite records those as ordinary expected/actual mismatches and
# prints a normal-looking tally: a mis-staged control reported 2 passed / 21
# failed where the truth was 14 / 9, with nothing to signal it was bogus
# (#2713). An inflated count overstates the fix's coverage, which is the one
# claim a positive control exists to make trustworthy.
#
# The discriminator is ref-agnostic on purpose: a real scripts/fleet/ always
# carries both halves, so fleet-* wrappers present with zero fleet_*.py
# modules beside them means the stage is partial. Anything else — a dir that
# isn't a fleet script dir at all, or a complete tree — returns 0.
require_fleet_lib_dir() {
    local dir="${1:-}"
    [[ -n "$dir" && -d "$dir" ]] || return 0
    compgen -G "$dir/fleet-*" >/dev/null 2>&1 || return 0
    compgen -G "$dir/fleet_*.py" >/dev/null 2>&1 && return 0

    echo "test setup: incomplete fleet script tree at $dir" >&2
    echo "            fleet-* wrappers are present but no fleet_*.py modules are," >&2
    echo "            so every invocation would abort on its lib-dir preflight and" >&2
    echo "            score as assertion failures. Stage the whole directory:" >&2
    echo "              git archive <ref> scripts/fleet | tar -x -C <tmpdir>" >&2
    echo "            or just use: fleet-positive-control <test-file> <ref>" >&2
    exit 2
}

# Zero-touch net for the suites that resolve the script under test through the
# SCRIPT_DIR convention: validate it here if it is already set, so no suite has
# to opt in. Suites that source this file before setting SCRIPT_DIR (or that use
# another name) are unaffected and can call require_fleet_lib_dir explicitly.
if [[ -n "${SCRIPT_DIR:-}" ]]; then
    require_fleet_lib_dir "$SCRIPT_DIR"
fi

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

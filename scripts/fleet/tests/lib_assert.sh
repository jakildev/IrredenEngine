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

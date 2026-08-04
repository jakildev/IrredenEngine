#!/usr/bin/env bash
# Tests for run_all.sh (#2712, #2786) — the runner that executes every
# fleet test suite in this directory.
#
# Hermetic: every case copies run_all.sh into a temp dir alongside SYNTHETIC
# fixture suites and runs it there. The runner discovers suites relative to
# its own dirname, so it never sees the real suites, never touches
# ~/.fleet, and never hits the network.
set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
RUNNER="$SCRIPT_DIR/run_all.sh"
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$RUNNER" ]]; then
    echo "test setup: run_all.sh not executable at $RUNNER" >&2
    exit 1
fi

TMPROOT=$(mktemp -d)
trap 'rm -rf "$TMPROOT"' EXIT

# Builds a sandbox dir holding a copy of the runner; caller adds fixtures.
new_sandbox() {  # $1 = sandbox name -> echoes the dir
    local d="$TMPROOT/$1"
    mkdir -p "$d"
    cp "$RUNNER" "$d/run_all.sh"
    chmod +x "$d/run_all.sh"
    echo "$d"
}

fixture_pass() { printf '#!/usr/bin/env bash\nexit 0\n' > "$1/test_$2.sh"; }
fixture_fail() { printf '#!/usr/bin/env bash\necho "boom in %s"\nexit 1\n' "$2" > "$1/test_$2.sh"; }
# exit 3 is the shared skip status (#2786) — a distinct fixture so it's
# never confused with fixture_fail's ordinary failure.
fixture_skip() { printf '#!/usr/bin/env bash\necho "SKIP: %s subject missing" >&2\nexit 3\n' "$2" > "$1/test_$2.sh"; }

echo "T1: all-passing suites exit 0 and are counted"
d=$(new_sandbox t1)
fixture_pass "$d" alpha
fixture_pass "$d" beta
printf 'import sys\nsys.exit(0)\n' > "$d/test_gamma.py"
out=$(bash "$d/run_all.sh" 2>&1); rc=$?
assert_eq "$rc" "0" "T1 exit 0 when every suite passes"
assert_contains "$out" "3 suite(s) — 3 passed, 0 failed, 0 skipped" "T1 summary counts all three"
assert_contains "$out" "PASS  test_alpha.sh" "T1 per-suite PASS line"
assert_contains "$out" "PASS  test_gamma.py" "T1 .py suite ran under python3"

echo "T2: one failing suite exits 1 and names the failure"
d=$(new_sandbox t2)
fixture_pass "$d" alpha
fixture_fail "$d" broken
out=$(bash "$d/run_all.sh" 2>&1); rc=$?
assert_eq "$rc" "1" "T2 exit 1 when a suite fails"
assert_contains "$out" "FAIL  test_broken.sh (exit 1)" "T2 FAIL line carries the exit code"
assert_contains "$out" "boom in broken" "T2 failing suite's output is echoed for triage"
assert_contains "$out" "1 passed, 1 failed, 0 skipped" "T2 summary counts the failure"
assert_contains "$out" "failed: test_broken.sh" "T2 names the failing suite"

echo "T3: a failing .py suite is caught too"
d=$(new_sandbox t3)
printf 'raise SystemExit(1)\n' > "$d/test_pybroken.py"
out=$(bash "$d/run_all.sh" 2>&1); rc=$?
assert_eq "$rc" "1" "T3 exit 1 on a failing python suite"
assert_contains "$out" "FAIL  test_pybroken.py" "T3 python failure reported"

echo "T4: lib_assert.sh is never executed as a suite"
d=$(new_sandbox t4)
fixture_pass "$d" alpha
printf '#!/usr/bin/env bash\nexit 9\n' > "$d/lib_assert.sh"
out=$(bash "$d/run_all.sh" 2>&1); rc=$?
assert_eq "$rc" "0" "T4 lib_assert.sh does not fail the run"
assert_absent "$out" "lib_assert" "T4 lib_assert.sh is not discovered"

echo "T5: --list prints suites without running them"
d=$(new_sandbox t5)
printf '#!/usr/bin/env bash\ntouch "%s/ran-marker"\nexit 0\n' "$d" > "$d/test_alpha.sh"
out=$(bash "$d/run_all.sh" --list 2>&1); rc=$?
assert_eq "$rc" "0" "T5 --list exits 0"
assert_contains "$out" "test_alpha.sh" "T5 --list names the suite"
assert_absent "$out" "PASS" "T5 --list does not report results"
if [[ -e "$d/ran-marker" ]]; then bad "T5 --list must not execute suites"; else ok "T5 --list executed nothing"; fi

echo "T6: --only filters, in both spellings"
d=$(new_sandbox t6)
fixture_pass "$d" alpha
fixture_fail "$d" broken
out=$(bash "$d/run_all.sh" --only alpha 2>&1); rc=$?
assert_eq "$rc" "0" "T6 --only alpha skips the failing suite"
assert_contains "$out" "1 suite(s) — 1 passed" "T6 --only narrows the set"
out=$(bash "$d/run_all.sh" --only=alpha 2>&1); rc=$?
assert_eq "$rc" "0" "T6 --only=alpha equals form behaves identically"

echo "T7: --only matching nothing is an error, not a vacuous pass"
d=$(new_sandbox t7)
fixture_pass "$d" alpha
out=$(bash "$d/run_all.sh" --only nosuchsuite 2>&1); rc=$?
assert_eq "$rc" "1" "T7 no match exits 1"
assert_contains "$out" "no suites matched" "T7 explains the empty selection"

echo "T8: usage errors exit 2"
d=$(new_sandbox t8)
fixture_pass "$d" alpha
out=$(bash "$d/run_all.sh" --bogus 2>&1); rc=$?
assert_eq "$rc" "2" "T8 unknown argument exits 2"
# Dual-spelling rule (scripts/fleet/CLAUDE.md): the equals arm must reject an
# empty value exactly as the space arm rejects a missing one.
out=$(bash "$d/run_all.sh" --only= 2>&1); rc=$?
assert_eq "$rc" "2" "T8 empty --only= rejected like a missing value"
out=$(bash "$d/run_all.sh" --only 2>&1); rc=$?
assert_eq "$rc" "2" "T8 --only with no value rejected"
out=$(bash "$d/run_all.sh" --timeout= 2>&1); rc=$?
assert_eq "$rc" "2" "T8 empty --timeout= rejected"
out=$(bash "$d/run_all.sh" --timeout abc 2>&1); rc=$?
assert_eq "$rc" "2" "T8 non-numeric --timeout rejected"

echo "T9: --help exits 0 and prints the usage block"
d=$(new_sandbox t9)
out=$(bash "$d/run_all.sh" --help 2>&1); rc=$?
assert_eq "$rc" "0" "T9 --help exits 0"
assert_contains "$out" "run_all.sh [--only <substring>]" "T9 --help prints usage"
assert_contains "$out" "Exit status:" "T9 --help reaches the end of the header"
# The header is sliced by shape, not a line range, so growing it can't leak
# code into the help text or truncate the block (#2436).
assert_absent "$out" "set -uo pipefail" "T9 --help stops before the code"
assert_absent "$out" "#!/usr/bin/env" "T9 --help omits the shebang"

echo "T10: a hung suite is killed by the per-suite timeout"
d=$(new_sandbox t10)
printf '#!/usr/bin/env bash\nsleep 30\n' > "$d/test_hang.sh"
if command -v timeout >/dev/null 2>&1 || command -v gtimeout >/dev/null 2>&1; then
    out=$(bash "$d/run_all.sh" --timeout 1 2>&1); rc=$?
    assert_eq "$rc" "1" "T10 a timed-out suite fails the run"
    assert_contains "$out" "timed out after 1s" "T10 timeout is reported distinctly"
else
    ok "T10 skipped — no timeout(1)/gtimeout(1) on this host"
fi

echo "T11: --timeout 0 disables the guard"
d=$(new_sandbox t11)
fixture_pass "$d" alpha
out=$(bash "$d/run_all.sh" --timeout 0 2>&1); rc=$?
assert_eq "$rc" "0" "T11 --timeout 0 still runs suites"
assert_contains "$out" "1 passed" "T11 --timeout 0 reports normally"

# #2786: a suite whose subject under test is missing exits 3 (the shared
# skip status), not 0 — a vacuous run must never be folded into "passed".
echo "T12: a skipping suite (exit 3) is counted separately, not as a pass"
d=$(new_sandbox t12)
fixture_pass "$d" alpha
fixture_skip "$d" skippy
out=$(bash "$d/run_all.sh" 2>&1); rc=$?
assert_eq "$rc" "0" "T12 exit 0 — a skip alone does not fail the run"
assert_contains "$out" "2 suite(s) — 1 passed, 0 failed, 1 skipped" "T12 summary distinguishes skipped from passed"
assert_contains "$out" "SKIP  test_skippy.sh" "T12 per-suite SKIP line"
assert_contains "$out" "skipped: test_skippy.sh" "T12 names the skipped suite"
assert_absent "$out" "PASS  test_skippy.sh" "T12 skip is never reported as PASS"

echo "T13: a real failure still fails the run alongside a skip (negative control)"
d=$(new_sandbox t13)
fixture_fail "$d" broken
fixture_skip "$d" skippy
out=$(bash "$d/run_all.sh" 2>&1); rc=$?
assert_eq "$rc" "1" "T13 exit 1 — a skip must not mask a real failure"
assert_contains "$out" "0 passed, 1 failed, 1 skipped" "T13 summary counts failures and skips independently"
assert_contains "$out" "failed: test_broken.sh" "T13 still names the real failure"

summarize "run_all.sh tests"

#!/usr/bin/env bash
# Tests for run_all.sh — the runner that executes every fleet test suite in
# this directory.
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
# exit 3 is the shared skip status — a distinct fixture so it's never
# confused with fixture_fail's ordinary failure.
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
assert_contains "$out" "::error title=fleet-tests failed suites::test_broken.sh@?" \
    "T2 the failed suite set is emitted as a check-run annotation; no failure line is unitemized"

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
# code into the help text or truncate the block.
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

# A suite whose subject under test is missing exits 3 (the shared skip
# status), not 0 — a vacuous run must never be folded into "passed".
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
annotation=$(printf '%s\n' "$out" | grep -a '^::error title=fleet-tests failed suites::')
assert_eq "$annotation" "::error title=fleet-tests failed suites::test_broken.sh@?" \
    "T13 a skipped suite is never annotated as failed"

# The runner sits one level under the wrapper in the real tree, so these two
# sandboxes nest it the same way. The wrapper is the REAL one: the scrub set is
# read from it, including a name assigned on an export continuation line.
WRAPPER="$SCRIPT_DIR/../fleet-dispatch-wrap"
new_nested_sandbox() {  # $1 = sandbox name -> echoes the tests dir
    local d="$TMPROOT/$1/tests"
    mkdir -p "$d"
    cp "$RUNNER" "$d/run_all.sh"
    cp "$WRAPPER" "$TMPROOT/$1/fleet-dispatch-wrap"
    echo "$d"
}
fixture_env_probe() {  # a suite that reports what it inherited, then fails
    printf '%s\n' '#!/usr/bin/env bash' \
        'echo "role=${FLEET_ROLE-unset} model=${FLEET_ROLE_MODEL-unset} number=${FLEET_DISPATCH_NUMBER-unset} keep=${UNRELATED_KEEP-unset}"' \
        'exit 1' > "$1/test_envprobe.sh"
}

echo "T14: the pane's FLEET_* exports never reach a suite"
if [[ -f "$WRAPPER" ]]; then
    d=$(new_nested_sandbox t14)
    fixture_env_probe "$d"
    out=$(FLEET_ROLE=worker FLEET_ROLE_MODEL=opus FLEET_DISPATCH_NUMBER=42 UNRELATED_KEEP=yes \
          bash "$d/run_all.sh" 2>&1); rc=$?
    assert_contains "$out" "role=unset model=unset number=unset" "T14 wrapper-exported names are scrubbed"
    assert_contains "$out" "keep=yes" "T14 unrelated environment is left alone"
else
    bad "T14 fleet-dispatch-wrap missing at $WRAPPER — the scrub set has no source"
fi

echo "T15: with no wrapper beside it the runner scrubs nothing (negative control)"
d=$(new_sandbox t15)
fixture_env_probe "$d"
out=$(FLEET_ROLE=worker FLEET_ROLE_MODEL=opus bash "$d/run_all.sh" 2>&1); rc=$?
assert_contains "$out" "role=worker model=opus" "T15 the scrub comes from the wrapper, not a blanket unset"

# The annotation token for suite $2 in run_all output $1.
suite_token() {
    printf '%s\n' "$1" | grep -a '^::error title=fleet-tests failed suites::' \
        | sed 's/^::error title=fleet-tests failed suites:://' | tr ' ' '\n' | grep -a "^$2@"
}
fixture_fail_lines() {  # $1 = dir, $2 = name, rest = lines the suite prints
    local d="$1" name="$2"
    shift 2
    { echo '#!/usr/bin/env bash'
      for line in "$@"; do printf 'echo %q\n' "$line"; done
      echo 'exit 1'; } > "$d/test_$name.sh"
}

echo "T16: a failed suite's identity is its failure lines, stable across runs"
d=$(new_sandbox t16a)
fixture_fail_lines "$d" item "  ok: fine in $d" "  FAIL: alpha broke"
printf '%s\n' 'import unittest' 'class T(unittest.TestCase):' \
    '    def test_x(self):' '        self.fail("x")' 'unittest.main()' > "$d/test_pyitem.py"
first=$(bash "$d/run_all.sh" 2>&1)
second=$(bash "$d/run_all.sh" 2>&1)
item_a=$(suite_token "$first" test_item.sh)
assert_contains "$item_a" "test_item.sh@" "T16 an itemized bash failure is annotated with an identity"
assert_absent "$item_a" "test_item.sh@?" "T16 a suite printing lib_assert FAIL lines is itemized"
py_a=$(suite_token "$first" test_pyitem.py)
assert_absent "$py_a" "test_pyitem.py@?" "T16 a unittest FAIL header is itemized"
assert_eq "$(suite_token "$second" test_item.sh) $(suite_token "$second" test_pyitem.py)" \
    "$item_a $py_a" "T16 the same failures give the same identity on a second run"

d=$(new_sandbox t16b)
fixture_fail_lines "$d" item "  ok: other output in $d" "  FAIL: alpha broke" "  FAIL: alpha broke"
assert_eq "$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_item.sh)" "$item_a" \
    "T16 only the distinct failure lines count, not the rest of the output"

d=$(new_sandbox t16c)
fixture_fail_lines "$d" item "  FAIL: alpha broke" "  FAIL: beta broke"
item_c=$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_item.sh)
assert_contains "$item_c" "test_item.sh@" "T16 a second failure is still itemized"
if [[ -n "$item_c" && "$item_c" != "$item_a" ]]; then
    ok "T16 a second failure in the same suite changes its identity"
else
    bad "T16 a second failure in the same suite changes its identity ($item_c vs $item_a)"
fi

d=$(new_sandbox t16d)
printf '#!/usr/bin/env bash\necho "  FAIL: alpha broke"\nsleep 30\n' > "$d/test_hang.sh"
if command -v timeout >/dev/null 2>&1 || command -v gtimeout >/dev/null 2>&1; then
    assert_eq "$(suite_token "$(bash "$d/run_all.sh" --timeout 1 2>&1)" test_hang.sh)" \
        "test_hang.sh@?" "T16 a timed-out suite is unitemized even with failure lines"
else
    ok "T16 timeout arm skipped — no timeout(1)/gtimeout(1) on this host"
fi

# Each pair below runs in ONE sandbox, rewriting the fixture between runs, so
# the suite path a traceback names is identical and only the detail differs.
assert_ids_differ() {  # $1 = id, $2 = other id, $3 = msg
    if [[ -n "$1" && "$1" != *@\? && "$1" != "$2" ]]; then ok "$3"; else bad "$3 ($1 vs $2)"; fi
}
unittest_fixture() {  # $1 = dir, $2 = the value compared with 1
    printf '%s\n' 'import unittest' 'class T(unittest.TestCase):' \
        '    def test_value(self):' "        self.assertEqual(1, $2)" 'unittest.main()' > "$1/test_pyval.py"
}
lib_assert_fixture() {  # $1 = dir, $2 = the actual value
    printf '%s\n' '#!/usr/bin/env bash' 'source "$(dirname "$0")/lib_assert.sh"' \
        "assert_eq \"$2\" 1 \"value matches\"" 'summarize "val"' > "$1/test_val.sh"
}

echo "T17: the identity carries the failure detail, not just the header"
d=$(new_sandbox t17a)
unittest_fixture "$d" 2
py_2=$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_pyval.py)
py_2b=$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_pyval.py)
unittest_fixture "$d" 3
py_3=$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_pyval.py)
assert_eq "$py_2b" "$py_2" "T17 one unittest assertion failing alike twice keeps its identity"
assert_ids_differ "$py_3" "$py_2" \
    "T17 the same unittest test failing a different assertEqual changes the identity"

d=$(new_sandbox t17b)
cp "$SCRIPT_DIR/lib_assert.sh" "$SCRIPT_DIR/lib_preflight.sh" "$d/"
lib_assert_fixture "$d" 2
val_2=$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_val.sh)
val_2b=$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_val.sh)
lib_assert_fixture "$d" 3
val_3=$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_val.sh)
assert_eq "$val_2b" "$val_2" "T17 one lib_assert failure repeated keeps its identity"
assert_ids_differ "$val_3" "$val_2" \
    "T17 one lib_assert message with a different actual value changes the identity"

d=$(new_sandbox t17c)
cat > "$d/test_tmp.sh" <<'EOF'
#!/usr/bin/env bash
t=$(mktemp -d); p=$(python3 -c 'import tempfile; print(tempfile.mkdtemp())')
echo "  FAIL: fixture dir unreadable"
echo "        in: $t and $p"
rm -rf "$t" "$p"
exit 1
EOF
tmp_a=$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_tmp.sh)
assert_eq "$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_tmp.sh)" "$tmp_a" \
    "T17 random mktemp/tempfile names in the detail do not change the identity"

d=$(new_sandbox t17d)
fixture_fail_lines "$d" item "  FAIL: alpha broke" "" "trailing $d $RANDOM"
assert_eq "$(suite_token "$(bash "$d/run_all.sh" 2>&1)" test_item.sh)" "$item_a" \
    "T17 output after the blank line closing a lib_assert block is not detail"

summarize "run_all.sh tests"

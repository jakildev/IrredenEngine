#!/usr/bin/env bash
# Tests for fleet-positive-control and lib_assert.sh's require_fleet_lib_dir
# guard (#2713).
#
# A positive control stages a pre-fix tree and runs the new suite against it.
# Staging only the script under test leaves the fleet-* wrappers unable to find
# the fleet_*.py modules they dispatch to, so every invocation aborts on its own
# lib-dir preflight — and the suite scored those as ordinary assertion failures
# and printed a normal-looking tally (2 passed / 21 failed where the truth was
# 14 / 9). These tests pin both halves of the fix: the guard makes a partial
# stage abort with no tally, and the wrapper makes correct staging the easy path.
#
# Purely local: no network, no ~/.fleet, no gh.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
WRAPPER="$SCRIPT_DIR/fleet-positive-control"
LIB_ASSERT="$SCRIPT_DIR/tests/lib_assert.sh"

[[ -x "$WRAPPER" ]] || { echo "test setup: fleet-positive-control not found at $WRAPPER" >&2; exit 2; }

# shellcheck source=lib_assert.sh
source "$LIB_ASSERT"

TMPROOT=""
STRAYS=()
cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
    # The wrapper only accepts a suite inside the repo, so the fixtures below
    # are written into tests/ and must not survive a failed run.
    for f in "${STRAYS[@]+"${STRAYS[@]}"}"; do rm -f "$f"; done
    return 0
}
trap cleanup EXIT

TMPROOT=$(mktemp -d)

OUT=""
RC=0
run() { set +e; OUT=$("$@" 2>&1); RC=$?; set -e; }

# --- the guard: require_fleet_lib_dir ---------------------------------------
# Driven through a fresh bash that sources lib_assert with SCRIPT_DIR set, which
# is exactly how the 23 conforming suites reach the auto-fire.
probe_guard() {
    local dir="$1" driver="$TMPROOT/driver.sh"
    cat > "$driver" <<DRIVER
set -euo pipefail
SCRIPT_DIR="$dir"
source "$LIB_ASSERT"
echo "REACHED-BODY"
summarize
DRIVER
    run bash "$driver"
}

echo "--- a partial stage (wrappers, no .py modules) aborts as a setup error ---"
PARTIAL="$TMPROOT/partial/scripts/fleet"
mkdir -p "$PARTIAL/tests"
cp "$SCRIPT_DIR/fleet-claim" "$PARTIAL/fleet-claim"
cp "$LIB_ASSERT" "$PARTIAL/tests/lib_assert.sh"
probe_guard "$PARTIAL"
assert_eq "$RC" "2" "partial stage exits 2 (setup failure, not a result)"
assert_contains "$OUT" "test setup: incomplete fleet script tree" "the abort names itself as a setup failure"
assert_contains "$OUT" "fleet-positive-control" "the abort points at the wrapper that avoids this"
assert_absent "$OUT" "REACHED-BODY" "the suite body never runs on a partial stage"
# The whole point: no plausible-looking tally to copy into a PR body.
assert_absent "$OUT" "passed:" "a partial stage prints NO pass/fail tally"

echo "--- a complete stage runs normally ---"
COMPLETE="$TMPROOT/complete"
git -C "$REPO_ROOT" archive HEAD scripts/fleet | tar -x -C "$COMPLETE" 2>/dev/null || {
    mkdir -p "$COMPLETE"; git -C "$REPO_ROOT" archive HEAD scripts/fleet | tar -x -C "$COMPLETE"; }
probe_guard "$COMPLETE/scripts/fleet"
assert_eq "$RC" "0" "complete stage exits 0"
assert_contains "$OUT" "REACHED-BODY" "the suite body runs on a complete stage"

echo "--- a dir that is not a fleet script dir is left alone ---"
NOTFLEET="$TMPROOT/notfleet"
mkdir -p "$NOTFLEET"
touch "$NOTFLEET/some-file.txt"
probe_guard "$NOTFLEET"
assert_eq "$RC" "0" "non-fleet dir does not trip the guard"
assert_contains "$OUT" "REACHED-BODY" "non-fleet dir reaches the suite body"

echo "--- .py modules present with no wrappers is not a partial stage ---"
PYONLY="$TMPROOT/pyonly"
mkdir -p "$PYONLY"
touch "$PYONLY/fleet_branch_match.py"
probe_guard "$PYONLY"
assert_eq "$RC" "0" "modules-without-wrappers does not trip the guard"

# --- the wrapper: fleet-positive-control ------------------------------------
echo "--- usage errors exit 2 ---"
run "$WRAPPER"
assert_eq "$RC" "2" "no arguments exits 2"
assert_contains "$OUT" "usage:" "no arguments prints usage"

run "$WRAPPER" "$SCRIPT_DIR/tests/test_positive_control.sh"
assert_eq "$RC" "2" "a missing ref exits 2"

run "$WRAPPER" "$TMPROOT/does-not-exist.sh" HEAD
assert_eq "$RC" "2" "a missing test file exits 2"
assert_contains "$OUT" "test file not found" "the missing-file error names the cause"

run "$WRAPPER" "$SCRIPT_DIR/tests/test_positive_control.sh" "definitely-not-a-ref-2713"
assert_eq "$RC" "2" "an unresolvable ref exits 2"
assert_contains "$OUT" "not a commit-ish" "the bad-ref error names the cause"

run "$WRAPPER" "$SCRIPT_DIR/tests/test_positive_control.sh" HEAD --bogus-flag
assert_eq "$RC" "2" "an unrecognized option is rejected, not ignored"
assert_contains "$OUT" "unknown option" "the rejected option names itself"

# Both spellings of --include reject an empty value identically. scripts/fleet's
# CLAUDE.md makes this a standing rule: a diverging equals arm lets
# `--include=$UNSET_VAR` slip an empty string past downstream guards (#2193).
run "$WRAPPER" "$SCRIPT_DIR/tests/test_positive_control.sh" HEAD --include
assert_eq "$RC" "2" "--include with no pathspec exits 2"
assert_contains "$OUT" "needs a pathspec" "the space form names the missing value"

run "$WRAPPER" "$SCRIPT_DIR/tests/test_positive_control.sh" HEAD --include=
assert_eq "$RC" "2" "--include= with an empty pathspec exits 2"
assert_contains "$OUT" "needs a pathspec" "the equals form names the missing value"

# Fixtures live in tests/ because the wrapper requires a suite inside the repo.
# Untracked, so `git archive HEAD` stages the tree WITHOUT them and the wrapper's
# copy-over is what puts each one in play — the real new-test/old-code flow.
echo "--- a suite that cannot distinguish the ref is reported VACUOUS ---"
VAC="$SCRIPT_DIR/tests/test_zz_tmp_vacuous_2713.sh"
STRAYS+=("$VAC")
cat > "$VAC" <<'FIXTURE'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"
assert_eq "a" "a" "a tautology holds on any ref"
summarize
FIXTURE
chmod +x "$VAC"
run "$WRAPPER" "$VAC" HEAD
assert_eq "$RC" "1" "a vacuous suite exits 1"
assert_contains "$OUT" "VACUOUS" "the vacuous verdict names itself"
rm -f "$VAC"

echo "--- a suite that does distinguish the ref is reported MEANINGFUL ---"
# The discriminator is an UNTRACKED marker beside the wrapper: `git archive <ref>`
# only ever emits tracked content, so the stage cannot contain it for any ref,
# while the working tree can. That models "a file the fix adds" without asking
# what the ref happens to hold.
#
# The obvious shortcut — assert the wrapper's own presence, absent from the
# pre-fix ref — is what this originally did, and it was self-invalidating: it
# discriminates only while this change is uncommitted. The moment the commit
# existed, HEAD carried the wrapper, the fixture scored 2-of-2 passing, and the
# four assertions below failed VACUOUS on the PR's own branch (and would have on
# master forever after). Keep the discriminator independent of the ref's content.
MEANMARK="$SCRIPT_DIR/fleet-zz-tmp-added-by-fix-2713"
STRAYS+=("$MEANMARK")
: > "$MEANMARK"
MEAN="$SCRIPT_DIR/tests/test_zz_tmp_meaningful_2713.sh"
STRAYS+=("$MEAN")
cat > "$MEAN" <<'FIXTURE'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"
if [[ -f "$SCRIPT_DIR/fleet-zz-tmp-added-by-fix-2713" ]]; then
    ok "the file the fix adds is present in this tree"
else
    bad "the file the fix adds is present in this tree"
fi
assert_eq "kept" "kept" "a non-regression assertion that holds on both refs"
summarize
FIXTURE
chmod +x "$MEAN"
run "$WRAPPER" "$MEAN" HEAD
assert_eq "$RC" "0" "a meaningful suite exits 0"
assert_contains "$OUT" "MEANINGFUL: 1 of 2 assertions fail" "the verdict reports the real counts"
assert_contains "$OUT" "(1 + 1 = 2, the full suite.)" "the PR-body line shows its own arithmetic"
assert_contains "$OUT" "For the PR body's test plan:" "the wrapper emits a copy-pasteable line"
rm -f "$MEAN" "$MEANMARK"

echo "--- --include stages a pathspec outside scripts/fleet ---"
# A suite that reads outside scripts/fleet is exactly the case --include exists
# for, and the discriminator has to be the staged tree itself: the fixture below
# scores 2-of-2 failing without --include and 1-of-2 with it, so the assertion
# proves the flag STAGED something rather than merely that it parsed.
OUTSIDE="docs/agents/FLEET.md"
if git -C "$REPO_ROOT" cat-file -e "HEAD:$OUTSIDE" 2>/dev/null; then
    ok "precondition: $OUTSIDE is tracked at HEAD (the --include probe target)"
else
    bad "precondition: $OUTSIDE is tracked at HEAD (the --include probe target)"
fi

INC="$SCRIPT_DIR/tests/test_zz_tmp_include_2713.sh"
STRAYS+=("$INC")
cat > "$INC" <<FIXTURE
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=\$(cd "\$(dirname "\$0")/.." && pwd)
source "\$(dirname "\$0")/lib_assert.sh"
if [[ -f "\$SCRIPT_DIR/../../$OUTSIDE" ]]; then
    ok "$OUTSIDE is present in the staged tree"
else
    bad "$OUTSIDE is present in the staged tree"
fi
assert_eq "x" "y" "a standing failure, so the verdict is MEANINGFUL either way"
summarize
FIXTURE
chmod +x "$INC"

# Without the flag: the outside path is absent, so both assertions fail. Pinning
# the no-flag case is what makes the two below evidence of staging.
run "$WRAPPER" "$INC" HEAD
assert_contains "$OUT" "MEANINGFUL: 2 of 2 assertions fail" "without --include the outside path is not staged"

run "$WRAPPER" "$INC" HEAD --include "$OUTSIDE"
assert_eq "$RC" "0" "--include <pathspec> runs the suite"
assert_contains "$OUT" "MEANINGFUL: 1 of 2 assertions fail" "--include <pathspec> stages the outside path"
assert_contains "$OUT" "staged scripts/fleet + $OUTSIDE" "the run reports what it staged, not just scripts/fleet"

# The equals arm must reach the same staged tree, not merely be accepted.
run "$WRAPPER" "$INC" HEAD "--include=$OUTSIDE"
assert_eq "$RC" "0" "--include=<pathspec> runs the suite"
assert_contains "$OUT" "MEANINGFUL: 1 of 2 assertions fail" "--include=<pathspec> stages the outside path identically"
rm -f "$INC"

# --- .py suites: interpreter dispatch and unittest's tally (#2848) ----------
# The .py suites carry no shebang (run_all.sh supplies python3), so exec-ing
# the staged file handed them to the shell, and the docstring prose became a
# syntax error the wrapper reported as a *staging* failure.
echo "--- a .py suite runs under python3, not the shell ---"
PYMARK="$SCRIPT_DIR/fleet-zz-tmp-added-by-fix-2848"
STRAYS+=("$PYMARK")
: > "$PYMARK"
PYMEAN="$SCRIPT_DIR/tests/test_zz_tmp_meaningful_2848.py"
STRAYS+=("$PYMEAN")
cat > "$PYMEAN" <<'FIXTURE'
"""A docstring whose prose (parentheses, quotes) is a syntax error to a shell.

Its presence is the regression test for the interpreter dispatch: under bash
this file dies before any test runs.
"""

import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MARK = os.path.join(HERE, "..", "fleet-zz-tmp-added-by-fix-2848")


class MeaningfulFixture(unittest.TestCase):
    def test_the_file_the_fix_adds_is_present(self):
        self.assertTrue(os.path.exists(MARK))

    def test_a_non_regression_assertion_holding_on_both_refs(self):
        self.assertEqual("kept", "kept")


if __name__ == "__main__":
    unittest.main()
FIXTURE
run "$WRAPPER" "$PYMEAN" HEAD
assert_eq "$RC" "0" "a meaningful .py suite exits 0"
assert_contains "$OUT" "running the suite under python3" "the run names the interpreter it dispatched to"
assert_contains "$OUT" "MEANINGFUL: 1 of 2 assertions fail" "unittest's tally is read into the same verdict"
assert_contains "$OUT" "(1 + 1 = 2, the full suite.)" "the PR-body line shows its own arithmetic"
# The pre-fix symptom, pinned: prose in the docstring read as shell syntax.
assert_absent "$OUT" "syntax error" "the suite is never handed to the shell"
assert_absent "$OUT" "printed no tally" "a .py suite produces a real tally, not a setup failure"
rm -f "$PYMEAN" "$PYMARK"

echo "--- a .sh suite still names bash, so the dispatch is not python-only ---"
SHKIND="$SCRIPT_DIR/tests/test_zz_tmp_shkind_2848.sh"
STRAYS+=("$SHKIND")
cat > "$SHKIND" <<'FIXTURE'
#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib_assert.sh"
assert_eq "a" "a" "a tautology, so this fixture only probes the dispatch"
summarize
FIXTURE
chmod +x "$SHKIND"
run "$WRAPPER" "$SHKIND" HEAD
assert_contains "$OUT" "running the suite under bash" "a .sh suite dispatches to bash"
rm -f "$SHKIND"

echo "--- a vacuous .py suite is reported VACUOUS, not MEANINGFUL ---"
PYVAC="$SCRIPT_DIR/tests/test_zz_tmp_vacuous_2848.py"
STRAYS+=("$PYVAC")
cat > "$PYVAC" <<'FIXTURE'
import unittest


class VacuousFixture(unittest.TestCase):
    def test_a_tautology_holds_on_any_ref(self):
        self.assertEqual("a", "a")


if __name__ == "__main__":
    unittest.main()
FIXTURE
run "$WRAPPER" "$PYVAC" HEAD
assert_eq "$RC" "1" "a vacuous .py suite exits 1"
assert_contains "$OUT" "VACUOUS" "the vacuous verdict names itself"
rm -f "$PYVAC"

echo "--- skipped tests are not counted as passes ---"
# unittest counts a skip in `Ran N` but it neither passed nor failed. Folding
# it into the pass column reports the control as broader than it ran — and a
# staged tree makes skips routine, since a guard on any path outside
# scripts/fleet trips (test_fleet_validate_roles.py does exactly this).
PYSKIP="$SCRIPT_DIR/tests/test_zz_tmp_skipped_2848.py"
STRAYS+=("$PYSKIP")
cat > "$PYSKIP" <<'FIXTURE'
import unittest


class SkipFixture(unittest.TestCase):
    def test_one_real_failure(self):
        self.assertTrue(False)

    def test_one_real_pass(self):
        self.assertEqual(1, 1)

    def test_one_skip(self):
        self.skipTest("carries no signal about the ref")


if __name__ == "__main__":
    unittest.main()
FIXTURE
run "$WRAPPER" "$PYSKIP" HEAD
assert_eq "$RC" "0" "a suite with a skip still yields a verdict"
assert_contains "$OUT" "(1 + 1 + 1 = 3, the full suite.)" "skips are their own term, so the arithmetic still closes"
assert_contains "$OUT" "1 skipped" "the verdict line surfaces the skip count"
assert_absent "$OUT" "(2 + 1 = 3" "a skip is never folded into the passing count"
rm -f "$PYSKIP"

echo "--- 'expected failures' are passes; 'unexpected successes' are failures ---"
# The parenthetical's keys embed each other: a bare `failures=` match reads
# "expected failures=2" — tests that failed exactly as designed — as two real
# failures, and misses the unexpected success, which is a real one. The counts
# below are chosen so the naive parse and the correct one disagree (2 vs 1).
PYXF="$SCRIPT_DIR/tests/test_zz_tmp_xfail_2848.py"
STRAYS+=("$PYXF")
cat > "$PYXF" <<'FIXTURE'
import unittest


class ExpectedFailureFixture(unittest.TestCase):
    @unittest.expectedFailure
    def test_xfail_a(self):
        self.assertTrue(False)

    @unittest.expectedFailure
    def test_xfail_b(self):
        self.assertTrue(False)

    @unittest.expectedFailure
    def test_unexpectedly_succeeds(self):
        pass

    def test_plain_pass(self):
        self.assertEqual(1, 1)


if __name__ == "__main__":
    unittest.main()
FIXTURE
run "$WRAPPER" "$PYXF" HEAD
assert_contains "$OUT" "MEANINGFUL: 1 of 4 assertions fail" "only the unexpected success counts as a failure"
assert_contains "$OUT" "(3 + 1 = 4, the full suite.)" "the two expected failures stay in the passing column"
rm -f "$PYXF"

echo "--- the verdict is read off unittest's own stream, not the merge ---"
# summarize writes to stdout and unittest to stderr. Merged, a block-buffered
# stdout line flushed at process exit lands *after* stderr's summary, so a
# `tail -1` for the verdict reads that line instead. The decoy below is the
# shape that costs the most: a bare "OK" turns a failing control VACUOUS.
PYDECOY="$SCRIPT_DIR/tests/test_zz_tmp_decoy_2848.py"
STRAYS+=("$PYDECOY")
cat > "$PYDECOY" <<'FIXTURE'
import unittest


class DecoyFixture(unittest.TestCase):
    def test_a_real_failure(self):
        self.assertTrue(False)

    def test_a_real_pass(self):
        self.assertEqual(1, 1)


# Buffered when stdout is a file, so this flushes after unittest's summary.
print("OK")

if __name__ == "__main__":
    unittest.main()
FIXTURE
run "$WRAPPER" "$PYDECOY" HEAD
assert_eq "$RC" "0" "a stdout decoy does not change the verdict"
assert_contains "$OUT" "MEANINGFUL: 1 of 2 assertions fail" "the real failure is still counted"
assert_absent "$OUT" "VACUOUS" "a bare 'OK' on stdout cannot launder a failing control"
rm -f "$PYDECOY"

echo "--- a .py suite that aborts before its summary is a setup failure ---"
PYNOTALLY="$SCRIPT_DIR/tests/test_zz_tmp_notally_2848.py"
STRAYS+=("$PYNOTALLY")
cat > "$PYNOTALLY" <<'FIXTURE'
raise SystemExit("dying at import, before unittest ever summarizes")
FIXTURE
run "$WRAPPER" "$PYNOTALLY" HEAD
assert_eq "$RC" "2" "no unittest summary exits 2 rather than inventing a result"
assert_contains "$OUT" "printed no tally" "the no-tally error names the cause"
assert_contains "$OUT" "before unittest's summary" "the message names the summary this suite type owes"
rm -f "$PYNOTALLY"

echo "--- a suite type the tool cannot drive names itself, not the staging ---"
# The failure mode #2848 is about: the cause was reported as staging, the one
# thing that was correct, so the natural operator response was to re-stage.
run "$WRAPPER" "$REPO_ROOT/README.md" HEAD
assert_eq "$RC" "2" "an unsupported suite type exits 2"
assert_contains "$OUT" "unsupported suite type" "the error names the suite type as the cause"
assert_absent "$OUT" "staged scripts/fleet" "it is rejected before staging, so staging is never implicated"

echo "--- a suite that aborts before summarize is a setup failure, not a result ---"
NOTALLY="$SCRIPT_DIR/tests/test_zz_tmp_notally_2713.sh"
STRAYS+=("$NOTALLY")
cat > "$NOTALLY" <<'FIXTURE'
#!/usr/bin/env bash
set -euo pipefail
echo "dying before any tally"
exit 3
FIXTURE
chmod +x "$NOTALLY"
run "$WRAPPER" "$NOTALLY" HEAD
assert_eq "$RC" "2" "no tally exits 2 rather than inventing a result"
assert_contains "$OUT" "printed no tally" "the no-tally error names the cause"
rm -f "$NOTALLY"

summarize "fleet-positive-control tests"

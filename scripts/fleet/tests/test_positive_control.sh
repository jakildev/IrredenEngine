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
# The last block covers interpreter dispatch (#2848): the wrapper used to exec
# the staged suite, which only works for a self-executing script. The 32
# test_*.py suites carry no shebang — run_all.sh supplies python3 — so the shell
# interpreted them and the first prose line of the module docstring came back as
# a syntax error, reported as a *staging* failure, the one thing that had gone
# right. Dispatch by extension needs a second tally parser, so those tests pin
# unittest's arithmetic alongside it.
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

# --- interpreter dispatch: .sh under bash, .py under python3 (#2848) ---------
echo "--- a .py suite runs under python3 and reports a real verdict ---"
# Same untracked-marker discriminator as the bash MEANINGFUL fixture above, and
# for the same reason: `git archive <ref>` only emits tracked content, so the
# stage cannot hold it for any ref while the working tree always can.
PYMARK="$SCRIPT_DIR/fleet-zz-tmp-added-by-fix-2848"
STRAYS+=("$PYMARK")
: > "$PYMARK"
PYMEAN="$SCRIPT_DIR/tests/test_zz_tmp_meaningful_2848.py"
STRAYS+=("$PYMEAN")
cat > "$PYMEAN" <<'FIXTURE'
"""A docstring whose first prose line carries (parentheses) and "quotes".

That is the shape the shell choked on when this file was exec'd rather than
handed to python3 — the #2848 symptom, reproduced deliberately.
"""
import unittest
from pathlib import Path

_FLEET = Path(__file__).parent.parent


class Control(unittest.TestCase):
    def test_marker_the_fix_adds_is_present(self):
        self.assertTrue((_FLEET / "fleet-zz-tmp-added-by-fix-2848").is_file())

    def test_non_regression_holds_on_both_refs(self):
        self.assertEqual("kept", "kept")


unittest.main()
FIXTURE
run "$WRAPPER" "$PYMEAN" HEAD
assert_eq "$RC" "0" "a meaningful .py suite exits 0"
assert_contains "$OUT" "Ran 2 tests" "the suite reaches python3 rather than the shell"
assert_absent "$OUT" "syntax error" "the shell never interprets the Python file (#2848)"
assert_contains "$OUT" "MEANINGFUL: 1 of 2 assertions fail" "unittest's trailer is parsed into real counts"
assert_contains "$OUT" "(1 + 1 = 2, the full suite.)" "the PR-body line shows its own arithmetic"
rm -f "$PYMEAN" "$PYMARK"

echo "--- a .py suite that cannot distinguish the ref is reported VACUOUS ---"
PYVAC="$SCRIPT_DIR/tests/test_zz_tmp_vacuous_2848.py"
STRAYS+=("$PYVAC")
cat > "$PYVAC" <<'FIXTURE'
import unittest


class Control(unittest.TestCase):
    def test_tautology_holds_on_any_ref(self):
        self.assertEqual("a", "a")


unittest.main()
FIXTURE
run "$WRAPPER" "$PYVAC" HEAD
assert_eq "$RC" "1" "a vacuous .py suite exits 1"
assert_contains "$OUT" "VACUOUS" "the vacuous verdict names itself"
rm -f "$PYVAC"

echo "--- unittest's tally: errors are failures, skips and expected failures are not ---"
PYMIX="$SCRIPT_DIR/tests/test_zz_tmp_tally_2848.py"
STRAYS+=("$PYMIX")
cat > "$PYMIX" <<'FIXTURE'
import unittest


class Control(unittest.TestCase):
    def test_pass_a(self):
        self.assertTrue(True)

    def test_pass_b(self):
        self.assertTrue(True)

    def test_plain_failure(self):
        self.assertEqual(1, 2)

    def test_error(self):
        raise RuntimeError("an exception distinguishes the trees too")

    @unittest.skip("a skip neither passed nor failed")
    def test_skipped(self):
        pass

    @unittest.expectedFailure
    def test_expected_failure(self):
        self.assertEqual(1, 2)


unittest.main()
FIXTURE
run "$WRAPPER" "$PYMIX" HEAD
# 6 ran: 1 failure + 1 error = 2 fail, 1 skip, and 3 pass (2 clean + the
# expected failure). `expected failures=` ends in the same word as `failures=`,
# so a needle without the wrapper's anchor scores that expected one as real.
assert_contains "$OUT" "MEANINGFUL: 2 of 6 assertions fail" "an error counts with the failures, an expected failure does not"
assert_contains "$OUT" "with **1** skipped. (3 + 2 + 1 = 6, the full suite.)" "the PR-body line accounts for the skip instead of calling it a pass"
rm -f "$PYMIX"

echo "--- a suite that runs zero assertions is a setup failure, not VACUOUS ---"
PYEMPTY="$SCRIPT_DIR/tests/test_zz_tmp_empty_2848.py"
STRAYS+=("$PYEMPTY")
cat > "$PYEMPTY" <<'FIXTURE'
import unittest

unittest.main()
FIXTURE
run "$WRAPPER" "$PYEMPTY" HEAD
assert_eq "$RC" "2" "zero assertions exits 2 rather than reporting a verdict"
assert_contains "$OUT" "reported 0 assertions" "the empty-run error names the cause"
assert_absent "$OUT" "VACUOUS" "an empty run never claims the suite failed to distinguish the refs"
rm -f "$PYEMPTY"

echo "--- the no-tally diagnostic names the interpreter it used ---"
PYABORT="$SCRIPT_DIR/tests/test_zz_tmp_abort_2848.py"
STRAYS+=("$PYABORT")
cat > "$PYABORT" <<'FIXTURE'
import this_module_does_not_exist_2848

print(this_module_does_not_exist_2848)
FIXTURE
run "$WRAPPER" "$PYABORT" HEAD
assert_eq "$RC" "2" "a .py suite that aborts before summarizing exits 2"
assert_contains "$OUT" "Ran as: python3" "the diagnostic names the interpreter, so a dispatch fault is legible"
assert_contains "$OUT" "needs --include" "the diagnostic lists the causes rather than pinning it on staging"
rm -f "$PYABORT"

echo "--- an unsupported suite type names itself, not the staging ---"
WEIRD="$SCRIPT_DIR/tests/test_zz_tmp_unsupported_2848.rb"
STRAYS+=("$WEIRD")
echo 'puts "never reached"' > "$WEIRD"
run "$WRAPPER" "$WEIRD" HEAD
assert_eq "$RC" "2" "an unsupported suite type exits 2"
assert_contains "$OUT" "unsupported suite type" "the error names the suite type as the cause"
# The refusal lands before mktemp/git archive, so staging cannot be misread as
# the culprit.
assert_absent "$OUT" "staged scripts/fleet" "the refusal comes before staging, so staging is never implicated"
rm -f "$WEIRD"

summarize "fleet-positive-control tests"

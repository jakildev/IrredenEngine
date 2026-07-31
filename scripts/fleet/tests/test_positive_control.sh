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
# Self-referential on purpose: fleet-positive-control does not exist in HEAD's
# tree yet, so this fixture fails there and passes here — a real positive
# control of this PR's own change.
MEAN="$SCRIPT_DIR/tests/test_zz_tmp_meaningful_2713.sh"
STRAYS+=("$MEAN")
cat > "$MEAN" <<'FIXTURE'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"
if [[ -x "$SCRIPT_DIR/fleet-positive-control" ]]; then
    ok "the wrapper is present in this tree"
else
    bad "the wrapper is present in this tree"
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
rm -f "$MEAN"

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

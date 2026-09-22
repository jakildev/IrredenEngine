#!/usr/bin/env bash
# run_all.sh — run every render-harness test suite in this directory once.
#
# These suites are unittest, not CMake tests, so `ctest` never sees them; this
# runner and the `render-harness-tests.yml` workflow that calls it are the only
# things that execute them. Keep that workflow wired — an unexecuted suite goes
# red silently.
#
# One process per suite, and that is the point. These suites put scripts/ on
# sys.path themselves so they can import the dashed-name subjects. Run in a
# shared interpreter (`python3 -m unittest discover`), one suite's insert
# satisfies the next one's import, so a suite that forgot the line still reports
# green — the mask this runner exists to prevent.
#
# The fleet tooling tree has a same-shaped runner at
# scripts/fleet/tests/run_all.sh with a wider option surface (--only, --list,
# --timeout, bash+python dispatch). This one stays minimal deliberately: every
# suite here is python, hermetic, and sub-second, so the shared core is a
# ~10-line loop rather than a library worth factoring out.
#
# Usage:
#   run_all.sh [<dir>]     # <dir> defaults to this script's directory
#
# The directory argument exists so CI can point the runner at a fixture
# directory and prove it still reports failure — a green runner looks identical
# whether it ran everything or nothing.
#
# Output: `PASS  <suite> (<N> tests)`, with `, <K> skipped` when any skipped. A
# suite that exits 0 but reports no `Ran N tests` line, or N = 0, is a FAIL.
#
# Exit status:
#   0  every suite passed
#   1  at least one suite failed (including one that ran no tests), or no suite
#      was found
set -uo pipefail

PROG=$(basename "$0")
TESTS_DIR=${1:-$(cd "$(dirname "$0")" && pwd)}

if [[ ! -d "$TESTS_DIR" ]]; then
    echo "$PROG: not a directory: $TESTS_DIR" >&2
    exit 1
fi

suites=()
for f in "$TESTS_DIR"/test_*.py; do
    [[ -f "$f" ]] || continue                       # unmatched glob
    suites+=("$f")
done

if [[ ${#suites[@]} -eq 0 ]]; then
    echo "$PROG: no test_*.py suites found in $TESTS_DIR" >&2
    exit 1
fi

passed=0
failed_names=()

for f in "${suites[@]}"; do
    name=$(basename "$f")
    # Each suite in its own interpreter — see the header. cwd is deliberately
    # left alone: the suites resolve their subjects from __file__, so a run
    # that depends on cwd is itself a defect.
    if out=$(python3 "$f" 2>&1); then
        # A suite that exits 0 has not necessarily run anything: an all-skipped
        # suite, or a file that never calls unittest.main(), reads green. Take
        # the count from unittest's own "Ran N test(s)" line, surface skips in
        # the PASS line, and fail a suite that ran nothing.
        ran=$(printf '%s\n' "$out" | sed -n 's/^Ran \([0-9][0-9]*\) tests\{0,1\} in .*/\1/p' | tail -n 1)
        if [[ -z "$ran" || "$ran" -eq 0 ]]; then
            failed_names+=("$name")
            printf 'FAIL  %s (no tests ran)\n' "$name"
            printf '%s\n' "$out" | sed 's/^/      | /'
            continue
        fi
        skipped=$(printf '%s\n' "$out" | sed -n 's/^OK.*skipped=\([0-9][0-9]*\).*/\1/p' | tail -n 1)
        passed=$((passed + 1))
        if [[ -n "$skipped" && "$skipped" -gt 0 ]]; then
            printf 'PASS  %s (%s tests, %s skipped)\n' "$name" "$ran" "$skipped"
        else
            printf 'PASS  %s (%s tests)\n' "$name" "$ran"
        fi
    else
        rc=$?
        failed_names+=("$name")
        printf 'FAIL  %s (exit %s)\n' "$name" "$rc"
        printf '%s\n' "$out" | sed 's/^/      | /'
    fi
done

echo
echo "$PROG: ${#suites[@]} suite(s) — $passed passed, ${#failed_names[@]} failed"
if [[ ${#failed_names[@]} -gt 0 ]]; then
    echo "$PROG: failed: ${failed_names[*]}" >&2
    exit 1
fi

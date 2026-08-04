#!/usr/bin/env bash
# Tests for .github/workflows/fleet-tests.yml's path filters (#2810).
#
# The workflow path-filters on scripts/fleet/** — the LOCATION of its
# suites, not the SUBJECTS they test. Three suites test files that live
# outside that path (test_format_changed_line_scoping.sh covers
# cmake/run_clang_format_changed.cmake; test_ir_build_dir_resolution.sh
# covers engine/tools/lib/concurrency_helpers.sh; test_fleet_transition.sh
# covers docs/agents/fleet-state-machine.json), so a PR touching only one
# of those subjects previously got no fleet-tests run at all — the only
# regression coverage for that subject silently never fired.
#
# This is deliberately a hardcoded ratchet, not a parse of each suite's
# variable assignments (same shape as header_global_baseline in
# cmake/run_header_convention_checks.cmake): it asserts every known
# out-of-tree subject is present in BOTH `paths:` blocks of the
# workflow (push and pull_request — GitHub Actions has no YAML anchors,
# so the two lists are hand-duplicated and can drift independently).

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
WORKFLOW="$SCRIPT_DIR/.github/workflows/fleet-tests.yml"

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -f "$WORKFLOW" ]]; then
    echo "SKIP: workflow under test not found at $WORKFLOW" >&2
    exit 3  # skip status — run_all.sh must not count this as a pass (#2786)
fi

# The out-of-tree subjects each suite actually needs triggered on. Extend
# this list (and add the matching suite to scripts/fleet/tests/) whenever
# a new suite tests a file outside scripts/fleet/.
OUT_OF_TREE_SUBJECTS=(
    'cmake/run_clang_format_changed.cmake'
    'engine/tools/lib/concurrency_helpers.sh'
    'docs/agents/fleet-state-machine.json'
)

# paths_block <file> <section> — the raw text of the named top-level `on:`
# sub-block (`push` or `pull_request`), from its own header line up to
# (not including) the next sibling key at the same two-space indent. A
# plain sed range keeps this independent of any YAML parser being present
# on the runner.
paths_block() {
    local file="$1" section="$2"
    sed -n "/^  ${section}:/,/^  [a-zA-Z_]\+:/p" "$file" | sed '1d;$d'
}

# missing_subjects <file> — prints one "<block> <subject>" line per
# (block, subject) pair NOT found in that block of <file>. Empty output
# means every subject is covered in both blocks. Pure check, no ok/bad —
# callers decide what the presence/absence of output means for them.
missing_subjects() {
    local file="$1"
    local push_block pr_block subject
    push_block=$(paths_block "$file" push)
    pr_block=$(paths_block "$file" pull_request)
    for subject in "${OUT_OF_TREE_SUBJECTS[@]}"; do
        printf '%s' "$push_block" | grep -qF -- "$subject" || echo "push $subject"
        printf '%s' "$pr_block" | grep -qF -- "$subject" || echo "pull_request $subject"
    done
}

echo "T1: the real workflow lists every out-of-tree subject in both paths: blocks"
real_missing=$(missing_subjects "$WORKFLOW")
assert_eq "$real_missing" "" "fleet-tests.yml: no missing out-of-tree subjects"

echo "T2: positive control — deleting a subject from the workflow makes the check fail"
MUTATED=$(mktemp -t fleet-tests-workflow-mutated.XXXXXX.yml)
trap 'rm -f "$MUTATED"' EXIT
# Drop every line naming the first subject from both blocks, in a fresh
# temp copy — the real file under test is never touched.
grep -vF "${OUT_OF_TREE_SUBJECTS[0]}" "$WORKFLOW" > "$MUTATED"

mutated_missing=$(missing_subjects "$MUTATED")
assert_contains "$mutated_missing" "push ${OUT_OF_TREE_SUBJECTS[0]}" \
    "control fires: mutated copy reports the deleted subject missing from push"
assert_contains "$mutated_missing" "pull_request ${OUT_OF_TREE_SUBJECTS[0]}" \
    "control fires: mutated copy reports the deleted subject missing from pull_request"
# The mutated copy's OTHER subjects must all still be found — isolates the
# control to the one deleted entry rather than a blanket empty-block bug.
# Looping the whole tail is what keeps the isolation complete as the list
# grows: a single-index assert would leave every later subject unexercised.
if (( ${#OUT_OF_TREE_SUBJECTS[@]} > 1 )); then
    for untouched in "${OUT_OF_TREE_SUBJECTS[@]:1}"; do
        assert_absent "$mutated_missing" "$untouched" \
            "control isolation: untouched $untouched is not reported missing"
    done
fi

echo "T3: restoring the deleted path returns the check to passing (the file itself, untouched, still passes)"
restored_missing=$(missing_subjects "$WORKFLOW")
assert_eq "$restored_missing" "" "fleet-tests.yml: unaffected by the mutated copy, still clean"

summarize "fleet-tests.yml workflow path ratchet"

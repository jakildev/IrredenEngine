#!/usr/bin/env bash
# Tests for .github/workflows/fleet-tests.yml's path filters (#2810).
#
# The workflow path-filters on scripts/** — broadly the LOCATION of its
# suites, not the SUBJECTS they test. Five suites test files that live
# outside that path (test_format_changed_line_scoping.sh covers
# cmake/run_clang_format_changed.cmake; test_ir_build_dir_resolution.sh
# covers engine/tools/lib/concurrency_helpers.sh; test_fleet_transition.sh
# covers docs/agents/fleet-state-machine.json; test_lint_rules_commands.py
# covers every doc under .claude/rules/ and docs/agents/;
# test_lint_python_registry.py covers ruff.toml), so a PR touching only one
# of those subjects previously got no fleet-tests run at all — the only
# regression coverage for that subject silently never fired.
#
# T4 covers the other half of the same gap: one subject is not a file but a
# TREE — lint_python_registry.py derives its population from _SCAN_ROOT
# (scripts/), so the workflow's own location filter has to be at least that
# wide or the registry ratchet misses files added outside scripts/fleet/**
# (#2859). That is a filter-vs-constant check, not a list membership one,
# so it lives in its own case rather than in OUT_OF_TREE_SUBJECTS.
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
# a new suite tests a file outside scripts/ (a subject inside scripts/ is
# already covered by the filter's scripts/** glob, #2859). An entry is
# matched as a literal substring of the block, so a subject whose suite
# covers a whole directory is listed as the glob the workflow actually
# carries — the two `**` entries below are test_lint_rules_commands.py's
# doc globs (#2823), not single files. `docs/agents/**` subsumes
# fleet-state-machine.json; the narrower entry stays so the ratchet keeps
# naming that subject even if the glob is ever tightened.
OUT_OF_TREE_SUBJECTS=(
    'cmake/run_clang_format_changed.cmake'
    'engine/tools/lib/concurrency_helpers.sh'
    'docs/agents/fleet-state-machine.json'
    '.claude/rules/**'
    'docs/agents/**'
    'ruff.toml'
)

# The registry ratchet's population root (lint_python_registry.py's
# _SCAN_ROOT). Read from the module rather than hardcoded here so widening
# the scan can never silently outgrow the workflow filter that triggers it.
REGISTRY_LINTER="$SCRIPT_DIR/scripts/fleet/lint_python_registry.py"

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

# missing_scan_root <file> <root> — prints one "<block>" line per paths:
# block of <file> that does NOT cover the tree <root> (trailing slash
# optional, e.g. "scripts/"). Coverage means the block carries that tree's
# own recursive glob; a glob for a subdirectory of it (scripts/fleet/**)
# deliberately does NOT count, since that is exactly the too-narrow filter
# this case exists to catch.
missing_scan_root() {
    local file="$1" root="${2%/}"
    local block section
    for section in push pull_request; do
        block=$(paths_block "$file" "$section")
        printf '%s' "$block" | grep -qF -- "'${root}/**'" || echo "$section"
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

echo "T4: the workflow's location filter covers lint_python_registry.py's _SCAN_ROOT"
# Fail rather than skip when the module is gone: T1-T3 have already run
# against a present subject, so exiting 3 here would discard real results,
# and passing silently would be the vacuous pass #2786 exists to prevent.
if [[ ! -f "$REGISTRY_LINTER" ]]; then
    bad "registry linter not found at $REGISTRY_LINTER (retire T4 with it)"
else
    SCAN_ROOT=$(sed -n 's/^_SCAN_ROOT[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$REGISTRY_LINTER" | head -1)
    if [[ -z "$SCAN_ROOT" ]]; then
        bad "could not read _SCAN_ROOT from $REGISTRY_LINTER"
    else
        ok "_SCAN_ROOT read from the module under test: $SCAN_ROOT"
        assert_eq "$(missing_scan_root "$WORKFLOW" "$SCAN_ROOT")" "" \
            "fleet-tests.yml: both paths: blocks cover the registry scan root ($SCAN_ROOT)"

        # Positive control — narrowing the filter back to its pre-#2859
        # value (scripts/fleet/**) must be reported, in both blocks.
        NARROWED=$(mktemp -t fleet-tests-workflow-narrowed.XXXXXX.yml)
        sed "s|'${SCAN_ROOT%/}/\*\*'|'${SCAN_ROOT%/}/fleet/**'|" "$WORKFLOW" > "$NARROWED"
        narrowed_missing=$(missing_scan_root "$NARROWED" "$SCAN_ROOT")
        assert_contains "$narrowed_missing" "push" \
            "control fires: narrowed copy reports push no longer covering $SCAN_ROOT"
        assert_contains "$narrowed_missing" "pull_request" \
            "control fires: narrowed copy reports pull_request no longer covering $SCAN_ROOT"
        rm -f "$NARROWED"
    fi
fi

summarize "fleet-tests.yml workflow path ratchet"

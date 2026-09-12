#!/usr/bin/env bash
# Tests for .github/workflows/fleet-tests.yml's path filters (#2810).
#
# The workflow path-filters on scripts/** — broadly the LOCATION of its
# suites, not the SUBJECTS they test. Ten suites test files that live
# outside that path (test_format_changed_line_scoping.sh covers
# cmake/run_clang_format_changed.cmake; test_format_changed_standalone.sh
# covers its sibling standalone CMake file; test_ir_build_dir_resolution.sh
# covers engine/tools/lib/concurrency_helpers.sh; test_fleet_transition.sh
# covers docs/agents/fleet-state-machine.json; test_lint_rules_commands.py
# covers every doc under .claude/rules/ and docs/agents/;
# test_lint_comment_refs.py covers .claude/skills/simplify/**;
# test_fleet_labels_check.sh covers the fleet state machine and label reference;
# test_fleet_pr_body_lint.py covers the commit-and-push PR-body procedures;
# test_workflow_paths_sync.sh covers the other path-filtered
# .github/workflows/*.yml files; test_lint_python_registry.py covers
# ruff.toml), so a PR touching only one of those subjects previously got
# no fleet-tests run at all — the only regression coverage for that subject
# silently never fired.
#
# T4 covers the other half of the same gap: one subject is not a file but a
# TREE — lint_python_registry.py derives its population from _SCAN_ROOT
# (scripts/), so the workflow's own location filter has to be at least that
# wide or the registry ratchet misses files added outside scripts/fleet/**
# (#2859). That is a filter-vs-constant check, not a list membership one,
# so it lives in its own case rather than in OUT_OF_TREE_SUBJECTS.
#
# What T1-T3 below assert is that every REGISTERED out-of-tree subject is
# present in BOTH `paths:` blocks of the workflow (push and pull_request —
# GitHub Actions has no YAML anchors, so the two lists are hand-duplicated and
# can drift independently).
#
# They do NOT assert that the registry is complete, and they structurally
# cannot: both the green run and the T2 positive control are computed FROM
# OUT_OF_TREE_SUBJECTS, so a subject nobody added is a subject neither looks
# at. This list is an *inclusion* list with no scan behind it — the opposite
# of header_global_baseline in cmake/run_header_convention_checks.cmake, which
# is an *exclusion* list riding on a tree-wide scan and therefore catches an
# unlisted item by default. The two are complementary, not the same shape.
#
# T5 covers the complement: it executes scripts/fleet/fleet_test_subjects.py,
# which derives the live subject population (path literals in every suite
# source, plus the derived both-blocks workflow set) and fails on any member
# this list does not cover.
#
# T5 is also the stricter of the two on the axis T1 does cover. T1 matches a
# subject as a literal SUBSTRING of the block, so a longer entry that happens
# to contain it reads as coverage — delete the 'CLAUDE.md' entry and the
# 'creations/CLAUDE.md' one keeps T1 green. The checker's F3 parses the block
# into list entries and accepts only an exact entry or a segment-bounded
# recursive glob, so it reports that deletion. T1 stays as the retained
# ratchet (and as T2's control surface); F3 is the authority.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
WORKFLOW="$SCRIPT_DIR/.github/workflows/fleet-tests.yml"

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -f "$WORKFLOW" ]]; then
    echo "SKIP: workflow under test not found at $WORKFLOW" >&2
    exit 3  # skip status — run_all.sh must not count this as a pass (#2786)
fi

# The out-of-tree subjects each suite actually needs triggered on. Extending
# this by hand is not something to remember: fleet_test_subjects.py (T5)
# derives the population and names any member this list misses, with the
# referencing suite quoted. Add what it reports.
#
# An entry covers a subject exactly, or as a segment-bounded recursive glob —
# so a subject whose suite covers a whole directory is listed as the glob the
# workflow actually carries. The two `**` entries are test_lint_rules_commands.py's
# doc globs (#2823), not single files. `docs/agents/**` subsumes both
# fleet-state-machine.json (test_fleet_transition.sh) and
# fleet-labels-reference.md (test_fleet_labels_check.sh); the narrower entries
# stay so the ratchet keeps naming those subjects even if the glob is ever
# tightened. `.claude/skills/simplify/**` is test_lint_comment_refs.py's
# Check07Scope subject: that class reads the Check 7 doc and the simplify index
# row and fails when either drops a source class the ratchet counts, so a PR
# that narrows Check 7 alone must still trigger this workflow.
#
# `cmake/run_clang_format_changed_standalone.cmake` and its sibling
# `cmake/run_clang_format_changed.cmake` are two subjects, not one: T1 matches
# an entry as a literal substring of the block and neither string contains the
# other, so the shorter entry does not cover the longer file.
#
# The .github/workflows/ entries are test_workflow_paths_sync.sh's subjects.
# They are the only members whose population is *derived* rather than fixed —
# that suite globs .github/workflows/*.yml and covers whichever files
# declare both a push: and a pull_request: paths: block. A derived subject has
# no path literal for the scan to find, so fleet_test_subjects.py re-derives
# that population itself (same glob, same both-blocks predicate) and checks it
# against this list; test_workflow_paths_sync.sh T6 asserts the two derivations
# agree. T4 in that suite checks the derived set against the workflow's paths:
# blocks and never reads this array, so it is not the registry cross-check.
#
# The long tail below is that sweep's output: every tracked file outside
# scripts/fleet/ that a suite source names, comments and synthetic fixtures
# included. That over-includes on purpose — the cost is an extra CI trigger,
# and the alternative is a silent false clean.
OUT_OF_TREE_SUBJECTS=(
    'cmake/run_clang_format_changed.cmake'
    'cmake/run_clang_format_changed_standalone.cmake'
    'engine/tools/lib/concurrency_helpers.sh'
    'docs/agents/fleet-state-machine.json'
    'docs/agents/fleet-labels-reference.md'
    '.claude/rules/**'
    '.claude/skills/simplify/**'
    '.claude/skills/commit-and-push/procedures/pr-body.md'
    '.claude/skills/commit-and-push/procedures/stackable-on.md'
    'docs/agents/**'
    '.github/workflows/engine-tests.yml'
    '.github/workflows/format-check.yml'
    '.github/workflows/header-checks.yml'
    '.github/workflows/perf-gate.yml'
    '.github/workflows/python-lint.yml'
    '.github/workflows/render-harness-tests.yml'
    'ruff.toml'
    '.clang-format'
    '.claude/agents/review-acceptance.md'
    '.claude/agents/role-merger.md'
    '.claude/commands/role-merger.md'
    '.claude/commands/role-opus-architect.md'
    '.claude/commands/role-opus-reviewer.md'
    '.claude/commands/role-worker.md'
    '.claude/settings.json'
    '.claude/skills/commit-and-push/SKILL.md'
    '.claude/skills/optimize/reference/big_wins.md'
    '.claude/skills/simplify/SKILL.md'
    '.fleet/plans/issue-1394.md'
    '.fleet/plans/issue-1596.md'
    '.fleet/plans/issue-1824.md'
    '.fleet/plans/issue-2197.md'
    '.fleet/plans/issue-667.md'
    '.github/workflows/auto-rereview.yml'
    '.github/workflows/fleet-tests.yml'
    '.gitignore'
    'AGENTS.md'
    'CLAUDE.md'
    'CMakeLists.txt'
    'CMakePresets.json'
    'README.md'
    'cmake/ir_quality_tools.cmake'
    'cmake/run_header_checks_standalone.cmake'
    'cmake/run_header_convention_checks.cmake'
    'cmake/run_metal_kernel_registry_check.cmake'
    'cmake/run_metal_scratch_consumer_check.cmake'
    'cmake/run_save_inventory_population_check.cmake'
    'creations/CLAUDE.md'
    'creations/editors/voxel_editor/main.cpp'
    'docs/design/claude-md-sharing.md'
    'docs/design/detached-revoxelize-world-light.md'
    'docs/design/skill-sharing.md'
    'docs/design/voxel-occlusion-culling.md'
    'engine/CLAUDE.md'
    'engine/render/CLAUDE.md'
    'engine/render/include/irreden/render/gl_wrap/GL.h'
    'engine/render/include/irreden/render/metal/metal_runtime.hpp'
    'engine/render/src/metal/metal_cocoa_bridge.mm'
    'engine/render/src/metal/metal_pipeline.cpp'
    'engine/render/src/opengl/opengl_shader.cpp'
    'engine/tools/bin/ir-build'
    'engine/tools/bin/ir-run'
    'engine/video/src/metal/video_backend.cpp'
    'engine/video/src/opengl/video_backend.cpp'
    'engine/world/include/irreden/world/save_component_inventory.hpp'
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

echo "T5: the registry is COMPLETE — every derived subject is covered"
# T1-T4 quantify over OUT_OF_TREE_SUBJECTS and over one named constant; none
# of them can see a subject nobody registered. fleet_test_subjects.py derives
# that population from the suite sources and the workflow glob, so this case
# is the only one here whose failure mode is "the list is missing something".
#
# Executed, not re-implemented: the module is the single copy of the grammar,
# and tests/test_fleet_test_subjects.py is its hermetic unit/control suite.
SUBJECTS_CHECK="$SCRIPT_DIR/scripts/fleet/fleet_test_subjects.py"
if [[ ! -f "$SUBJECTS_CHECK" ]]; then
    bad "subject discovery checker not found at $SUBJECTS_CHECK (retire T5 with it)"
else
    subjects_out=$(python3 "$SUBJECTS_CHECK" "$SCRIPT_DIR" 2>&1)
    subjects_rc=$?
    case "$subjects_rc" in
        0) ok "fleet_test_subjects.py: registry covers every derived subject"
           # Print the coverage line so a green run carries its own proof of
           # having scanned something (a zero-finding run over zero sources
           # would otherwise be indistinguishable from a real pass).
           printf '%s\n' "$subjects_out" | head -1 | sed 's/^/        /' ;;
        1) bad "fleet_test_subjects.py reported unregistered subject(s)"
           printf '%s\n' "$subjects_out" | sed 's/^/        /' ;;
        *) bad "fleet_test_subjects.py aborted as a setup error (exit $subjects_rc)"
           printf '%s\n' "$subjects_out" | sed 's/^/        /' ;;
    esac
fi

summarize "fleet-tests.yml workflow path ratchet"

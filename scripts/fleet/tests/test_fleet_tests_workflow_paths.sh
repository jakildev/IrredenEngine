#!/usr/bin/env bash
# Tests for .github/workflows/fleet-tests.yml's detector coverage list.
#
# The list includes `scripts/**` because lint_python_registry.py scans that
# whole tree. It also includes subjects outside scripts/ that the fleet suites
# inspect, including the path-filtered workflows discovered by
# test_workflow_paths_sync.sh. T1-T4 exercise the registered list and scan-root
# width; T5 derives the subject population independently and reports omissions.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
# shellcheck source=lib_fleet_workflow_paths.sh
source "$(dirname "$0")/lib_fleet_workflow_paths.sh"
WORKFLOW="$SCRIPT_DIR/.github/workflows/fleet-tests.yml"

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -f "$WORKFLOW" ]]; then
    echo "SKIP: workflow under test not found at $WORKFLOW" >&2
    exit 3  # skip status — run_all.sh must not count this as a pass
fi

# The out-of-tree subjects each suite actually needs triggered on.
# fleet_test_subjects.py (T5) names any member this list misses, with the
# referencing suite quoted; add what it reports.
#
# An entry covers a subject exactly, or as a segment-bounded recursive glob —
# so a subject whose suite covers a whole directory is listed as the glob the
# workflow actually carries. The two `**` entries are test_lint_rules_commands.py's
# doc globs, not single files. `docs/agents/**` subsumes
# fleet-state-machine.json (test_fleet_transition.sh),
# fleet-labels-reference.md (test_fleet_labels_check.sh) and
# CLAUDE-BASELINE.md (test_lint_rules_registry.py); the narrower entries
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
    '.codex/**'
    'cmake/run_clang_format_changed.cmake'
    'cmake/run_clang_format_changed_standalone.cmake'
    'cmake/ir_quality_tools.cmake'
    'engine/tools/bin/ir-perf-grid'
    'engine/tools/lib/concurrency_helpers.sh'
    'engine/tools/lib/quiet_window.py'
    'docs/agents/fleet-state-machine.json'
    'docs/agents/fleet-labels-reference.md'
    'docs/agents/CLAUDE-BASELINE.md'
    '.claude/rules/**'
    '.claude/skills/simplify/**'
    '.claude/skills/commit-and-push/procedures/pr-body.md'
    '.claude/skills/commit-and-push/procedures/stackable-on.md'
    '.claude/skills/commit-and-push/procedures/native-stack-link.md'
    '.claude/skills/review-pr/procedures/stacked-pr-review.md'
    'docs/agents/**'
    '.github/workflows/engine-tests.yml'
    '.github/workflows/format-check.yml'
    '.github/workflows/header-checks.yml'
    '.github/workflows/perf-gate.yml'
    '.github/workflows/pr-closes-lint.yml'
    '.github/workflows/comment-refs.yml'
    '.github/workflows/instruction-size.yml'
    '.github/workflows/no-plan-files.yml'
    '.github/workflows/python-lint.yml'
    '.github/workflows/render-harness-tests.yml'
    '.github/workflows/shader-parity.yml'
    'ruff.toml'
    '.clang-format'
    '.claude/agents/review-acceptance.md'
    '.claude/agents/role-merger.md'
    '.claude/commands/role-merger.md'
    '.claude/commands/role-opus-architect.md'
    '.claude/commands/role-opus-reviewer.md'
    '.claude/commands/role-triage.md'
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
    'cmake/run_glsl_reserved_word_check.cmake'
    'cmake/run_header_checks_standalone.cmake'
    'cmake/run_header_convention_checks.cmake'
    'cmake/run_metal_kernel_registry_check.cmake'
    'cmake/run_metal_scratch_consumer_check.cmake'
    'cmake/run_save_inventory_population_check.cmake'
    'creations/CLAUDE.md'
    'creations/editors/font_maker/creation.json'
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
    'engine/tools/bin/ir-acquire'
    'engine/tools/bin/ir-build'
    'engine/tools/bin/ir-run'
    'engine/tools/bin/ir-launcher'
    'engine/tools/concurrency.toml'
    'engine/video/src/metal/video_backend.cpp'
    'engine/video/src/opengl/video_backend.cpp'
    'engine/world/include/irreden/world/save_component_inventory.hpp'
    'scripts/perf/perf_grid_matrix.sh'
    'scripts/perf/repeat_profile.py'
    'scripts/render-verify.py'
)

# The registry ratchet's population root (lint_python_registry.py's
# _SCAN_ROOT). Read from the module rather than hardcoded here so widening
# the scan can never silently outgrow the workflow filter that triggers it.
REGISTRY_LINTER="$SCRIPT_DIR/scripts/fleet/lint_python_registry.py"

# missing_subjects <file> — prints one subject missing from the detector's
# coverage list. Empty output means every subject is covered.
missing_subjects() {
    local file="$1" entries subject
    entries=$(fleet_coverage_list "$file")
    for subject in "${OUT_OF_TREE_SUBJECTS[@]}"; do
        printf '%s' "$entries" | grep -qF -- "$subject" || echo "$subject"
    done
}

# missing_scan_root <file> <root> — prints `covered` when the detector's
# coverage list does not include the tree's own recursive glob.
missing_scan_root() {
    local file="$1" root="${2%/}" entries
    entries=$(fleet_coverage_list "$file")
    printf '%s' "$entries" | grep -qF -- "'${root}/**'" || echo "covered"
}

detector_setup_errors() {
    local file="$1" changes_block checkout_line filter_line
    changes_block=$(awk '
        /^  changes:$/ { in_job=1 }
        in_job && /^  [A-Za-z0-9_-]+:$/ && $0 != "  changes:" { exit }
        in_job { print }
    ' "$file")
    printf '%s\n' "$changes_block" | grep -q '^    permissions:$' \
        || echo "permissions"
    printf '%s\n' "$changes_block" | grep -q '^      contents: read$' \
        || echo "contents"
    printf '%s\n' "$changes_block" | grep -q '^      pull-requests: read$' \
        || echo "pull-requests"
    checkout_line=$(printf '%s\n' "$changes_block" | grep -n 'uses: actions/checkout@v4' | cut -d: -f1)
    filter_line=$(printf '%s\n' "$changes_block" | grep -n 'uses: dorny/paths-filter@v3' | cut -d: -f1)
    if [[ -z "$checkout_line" || -z "$filter_line" || "$checkout_line" -ge "$filter_line" ]]; then
        echo "checkout-before-filter"
    fi
}

echo "T0: the detector can inspect both pull requests and checked-out push refs"
setup_errors=$(detector_setup_errors "$WORKFLOW")
assert_eq "$setup_errors" "" "changes job grants read permissions and checks out before filtering"
SETUP_MUTATED=$(mktemp -t fleet-tests-detector-setup.XXXXXX.yml)
grep -vE 'actions/checkout@v4|pull-requests: read' "$WORKFLOW" > "$SETUP_MUTATED"
mutated_setup_errors=$(detector_setup_errors "$SETUP_MUTATED")
assert_contains "$mutated_setup_errors" "pull-requests" \
    "control fires: missing pull-request permission is reported"
assert_contains "$mutated_setup_errors" "checkout-before-filter" \
    "control fires: missing checkout before the filter is reported"
rm -f "$SETUP_MUTATED"

echo "T1: the real workflow lists every out-of-tree subject in the detector"
real_missing=$(missing_subjects "$WORKFLOW")
assert_eq "$real_missing" "" "fleet-tests.yml: detector covers every out-of-tree subject"

echo "T2: positive control — deleting a subject from the workflow makes the check fail"
MUTATED=$(mktemp -t fleet-tests-workflow-mutated.XXXXXX.yml)
trap 'rm -f "$MUTATED"' EXIT
# Drop every line naming the first subject from both blocks, in a fresh
# temp copy — the real file under test is never touched.
grep -vF "${OUT_OF_TREE_SUBJECTS[0]}" "$WORKFLOW" > "$MUTATED"

mutated_missing=$(missing_subjects "$MUTATED")
assert_contains "$mutated_missing" "${OUT_OF_TREE_SUBJECTS[0]}" \
    "control fires: mutated copy reports the deleted subject missing"
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
# and passing silently would be the vacuous pass this guard exists to prevent.
if [[ ! -f "$REGISTRY_LINTER" ]]; then
    bad "registry linter not found at $REGISTRY_LINTER (retire T4 with it)"
else
    SCAN_ROOT=$(sed -n 's/^_SCAN_ROOT[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$REGISTRY_LINTER" | head -1)
    if [[ -z "$SCAN_ROOT" ]]; then
        bad "could not read _SCAN_ROOT from $REGISTRY_LINTER"
    else
        ok "_SCAN_ROOT read from the module under test: $SCAN_ROOT"
        assert_eq "$(missing_scan_root "$WORKFLOW" "$SCAN_ROOT")" "" \
            "fleet-tests.yml: detector covers the registry scan root ($SCAN_ROOT)"

        # Positive control — narrowing the filter back to its earlier,
        # narrower value (scripts/fleet/**) must be reported, in both blocks.
        NARROWED=$(mktemp -t fleet-tests-workflow-narrowed.XXXXXX.yml)
        sed "s|'${SCAN_ROOT%/}/\*\*'|'${SCAN_ROOT%/}/fleet/**'|" "$WORKFLOW" > "$NARROWED"
        narrowed_missing=$(missing_scan_root "$NARROWED" "$SCAN_ROOT")
        assert_contains "$narrowed_missing" "covered" \
            "control fires: narrowed copy reports the detector no longer covering $SCAN_ROOT"
        rm -f "$NARROWED"
    fi
fi

echo "T5: the registry is COMPLETE — every derived subject is covered"
# The one case here that reports a subject the list lacks. Executed, not
# re-implemented: the module is the single copy of the grammar, and
# tests/test_fleet_test_subjects.py is its unit/control suite.
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

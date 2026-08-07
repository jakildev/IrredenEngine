#!/usr/bin/env bash
# Every path-filtered .github/workflows/*.yml hand-duplicates its paths:
# list across the push: and pull_request: on: blocks (GitHub Actions has
# no YAML anchors). A hand-edit that adds a path to one block and forgets
# the other silently halves that workflow's trigger coverage, and the half
# that goes missing is normally pull_request — the block that gates the
# merge. #2810 built this ratchet for fleet-tests.yml only
# (test_fleet_tests_workflow_paths.sh); this suite generalizes it to every
# workflow in the tree.
#
# This is a different axis than that suite's OUT_OF_TREE_SUBJECTS ratchet.
# OUT_OF_TREE_SUBJECTS asks "which subjects does fleet-tests.yml list at
# all" (subject-domain completeness); this suite asks "do the two blocks
# agree on whatever a workflow DOES list" (workflow-scope sync), for every
# workflow, not just fleet-tests.yml. Neither subsumes the other.
#
# The workflow population is derived from a .github/workflows/*.yml glob,
# not a hardcoded list — #2876 records that a hardcoded inclusion list is
# invisible to both a green run and a positive control (both are computed
# *from* the list, so a workflow nobody added to it is a workflow the
# check never looks at). Deriving the population sidesteps that failure
# mode: a new workflow is covered automatically the moment it declares
# both blocks.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
WORKFLOWS_DIR="$REPO_ROOT/.github/workflows"

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -d "$WORKFLOWS_DIR" ]]; then
    echo "SKIP: workflows dir not found at $WORKFLOWS_DIR" >&2
    exit 3  # skip status — run_all.sh must not count this as a pass (#2786)
fi

# paths_list <file> <section> — the paths: list entries (one per output
# line, "      - '<glob>'" verbatim) under the named top-level on:
# sub-block (push or pull_request). Bounded to the paths: list itself,
# not the whole on: sub-block: most workflows have a sibling event key
# (workflow_dispatch:) that would stop a wider range scan, but
# perf-gate.yml has none — its pull_request: block runs straight into the
# top-level permissions: key, whose own sub-keys (`  contents: write`) are
# themselves 2-space indented and would be misread as workflow content by
# a scan keyed on indent alone. Stopping at the first line that isn't a
# "      - " list item sidesteps that regardless of what follows.
paths_list() {
    local file="$1" section="$2"
    awk -v section="$section" '
        $0 ~ "^  " section ":" { in_section=1; next }
        in_section && /^  [a-zA-Z_]+:/ { in_section=0 }
        in_section && /^    paths:/ { in_paths=1; next }
        in_section && in_paths && /^      - / { print; next }
        in_section && in_paths { in_paths=0 }
    ' "$file"
}

# is_covered <file> — true when the workflow declares both push: and
# pull_request: paths: lists (the sync check applies to it at all). A
# workflow with no paths: filter at all (auto-rereview.yml, quality.yml)
# or only one of the two blocks is out of scope, not a violation.
is_covered() {
    local file="$1"
    [[ -n "$(paths_list "$file" push)" && -n "$(paths_list "$file" pull_request)" ]]
}

# paths_diff <file> — empty when a covered workflow's push and
# pull_request lists carry the same entries; the unified diff otherwise.
# Order-independent (sorted before compare) — a re-sort of an identical
# set is not drift, only an added/removed/changed entry is.
paths_diff() {
    local file="$1" push_sorted pr_sorted
    push_sorted=$(paths_list "$file" push | sort)
    pr_sorted=$(paths_list "$file" pull_request | sort)
    [[ "$push_sorted" == "$pr_sorted" ]] && return 0
    diff <(printf '%s\n' "$push_sorted") <(printf '%s\n' "$pr_sorted")
}

# first_pr_entry_line <file> — the line number (in <file>) of the first
# paths: entry inside the pull_request: block specifically. Targeting by
# line number (rather than by matching the entry's text) is what keeps
# this correct when push and pull_request are in sync: every entry's text
# occurs twice in the file (once per block), so a text match would risk
# deleting the push occurrence instead of the pull_request one.
first_pr_entry_line() {
    local file="$1"
    awk '
        /^  pull_request:/ { in_section=1; next }
        in_section && /^  [a-zA-Z_]+:/ { in_section=0 }
        in_section && /^    paths:/ { in_paths=1; next }
        in_section && in_paths && /^      - / { print NR; exit }
    ' "$file"
}

mapfile -t all_workflows < <(find "$WORKFLOWS_DIR" -maxdepth 1 -name '*.yml' -type f | sort)

covered_workflows=()
for f in "${all_workflows[@]}"; do
    is_covered "$f" && covered_workflows+=("$f")
done

echo "T0: coverage — derived workflow set is non-empty"
if [[ ${#all_workflows[@]} -eq 0 ]]; then
    bad "found zero workflow files under $WORKFLOWS_DIR — glob is broken"
else
    ok "found ${#all_workflows[@]} workflow file(s) total, ${#covered_workflows[@]} declare both push: and pull_request: paths: lists"
fi
if [[ ${#covered_workflows[@]} -eq 0 ]]; then
    bad "zero workflows are covered — nothing below would ever be asserted"
fi

echo "T1: every covered workflow's push: and pull_request: paths: lists match"
for f in "${covered_workflows[@]}"; do
    name=$(basename "$f")
    d=$(paths_diff "$f")
    assert_eq "$d" "" "$name: push/pull_request paths: lists in sync"
done

echo "T2: positive control — deleting one pull_request path reports that workflow, per-workflow"
MUTATED=$(mktemp -d -t workflow-paths-sync-control.XXXXXX)
trap 'rm -rf "$MUTATED"' EXIT
for f in "${covered_workflows[@]}"; do
    name=$(basename "$f")
    lineno=$(first_pr_entry_line "$f")
    if [[ -z "$lineno" ]]; then
        bad "$name: control setup failed — could not locate a pull_request paths: entry to delete"
        continue
    fi
    entry=$(sed -n "${lineno}p" "$f")
    dst="$MUTATED/$name"
    awk -v skip="$lineno" 'NR==skip{next} {print}' "$f" > "$dst"

    d=$(paths_diff "$dst")
    assert_contains "$d" "$entry" \
        "$name: control fires — deleting a pull_request entry is reported as drift"
done

summarize "workflow push/pull_request paths: sync"

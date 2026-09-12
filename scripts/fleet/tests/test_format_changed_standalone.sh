#!/usr/bin/env bash
# Positive control for cmake/run_clang_format_changed_standalone.cmake (#3187).
#
# The shim is what gives the changed-lines formatter a CI path: the executor it
# wraps hard-requires QUALITY_FILE_LIST, a configure-time artifact, and the
# shim is what supplies it without a configure.
# .github/workflows/format-check.yml is its only caller.
#
# A green format gate looks identical whether it scanned the PR's diff, the
# wrong diff, or nothing at all, so each of those failure modes — invisible in
# a passing run — gets an arm:
#
#   - the shim runs with no configure at all and collects a non-empty list
#   - it collects the NARROW list (no INCLUDE_RENDER_BACKENDS), so the gate
#     never rewrites the generated GL wrapper or the Metal backend, which are
#     excluded from formatting on purpose (.claude/rules/cpp-globals.md
#     §Detection/Scope lists this call among the legitimate bare ones)
#   - FORMAT_DIFF_BASE wins over the @{upstream} probe rather than losing to
#     it, and moves BOTH the file set and the line ranges — parameterizing
#     only the three-dot file-list range yields a gate that picks the right
#     files and the wrong lines
#   - an unresolvable FORMAT_DIFF_BASE is fatal, not a silent "nothing to
#     format" pass, which would be a gate that goes green by looking at
#     nothing
#
# Fixture-based like test_header_checks_standalone.sh, so the suite stays
# hermetic and never formats anything in engine/.
# Line scoping itself is the sibling suite's subject
# (test_format_changed_line_scoping.sh); this one is about the entry point.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
SHIM="$SCRIPT_DIR/cmake/run_clang_format_changed_standalone.cmake"
STYLE_FILE="$SCRIPT_DIR/.clang-format"

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -f "$SHIM" ]]; then
    echo "SKIP: shim under test not found at $SHIM" >&2
    exit 3  # skip status — run_all.sh must not count this as a pass (#2786)
fi
# CI pins an exact clang-format and exports CLANG_FORMAT_BIN; locally the
# agent's own binary is fine — the arms below assert scoping, not a version's
# opinions.
CF_BIN="${CLANG_FORMAT_BIN:-clang-format}"
if ! command -v "$CF_BIN" >/dev/null 2>&1; then
    echo "SKIP: clang-format not available ($CF_BIN)" >&2
    exit 0
fi
for tool in cmake git; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "SKIP: $tool not available" >&2
        exit 0
    fi
done

TMPROOT=$(mktemp -d)
trap 'rm -rf "$TMPROOT"' EXIT

# A misformatted line, distinctive enough to grep for before and after.
BAD_B='int  b( int x ){return x   +2;}'
BAD_C='int  c( int x ){return x   +3;}'

# The shim identifies a tree as the repo root by cmake/ir_quality_tools.cmake,
# then globs the same four search roots the real collector does. Nothing else
# from the repo is needed.
make_fixture() {
    local root="$1"
    mkdir -p "$root/cmake" "$root/engine/include/irreden" \
             "$root/engine/render/include/irreden/render/gl_wrap"
    cp "$SCRIPT_DIR/cmake/ir_quality_tools.cmake" \
       "$SCRIPT_DIR/cmake/run_clang_format_changed.cmake" \
       "$SHIM" "$root/cmake/"
    cp "$STYLE_FILE" "$root/.clang-format"
    printf '#pragma once\nint a(int x) { return x + 1; }\n// MARKER\n' \
        > "$root/engine/include/irreden/plain.hpp"
    printf '#pragma once\nint a(int x) { return x + 1; }\n// MARKER\n' \
        > "$root/engine/render/include/irreden/render/gl_wrap/wrapped.hpp"
    git -C "$root" init -q .
    git -C "$root" config user.email t@example.com
    git -C "$root" config user.name test
    git -C "$root" add -A
    git -C "$root" commit -qm init
    git -C "$root" update-ref refs/remotes/origin/master HEAD
}

# run_shim <root> [extra -D args...]
run_shim() {
    local root="$1"; shift
    cmake -DPROJECT_ROOT="$root" \
          -DCLANG_FORMAT_BIN="$(command -v "$CF_BIN")" \
          "$@" \
          -P "$root/cmake/run_clang_format_changed_standalone.cmake" 2>&1
}

echo "T1: the shim runs in script mode — no configure, no compiler — and collects files"
REPO="$TMPROOT/t1"; mkdir -p "$REPO"; make_fixture "$REPO"
# Touch a line so there is something to do; the point of this arm is the
# entry point, not the edit.
printf '#pragma once\nint a(int x) { return x + 1; }\n// MARKER edited\n' \
    > "$REPO/engine/include/irreden/plain.hpp"
OUT=$(run_shim "$REPO")
RC=$?
assert_eq "$RC" "0" "shim exits 0 with no CMake configure anywhere"
assert_absent "$OUT" "QUALITY_FILE_LIST is required" \
    "the shim supplies the configure-time file list the executor demands"
assert_contains "$OUT" "file(s) on the quality list" \
    "reports how many files it collected (a green run proves it looked)"
COLLECTED=$(printf '%s' "$OUT" | sed -n 's/.*standalone): \([0-9]*\) file(s).*/\1/p')
if [[ -n "$COLLECTED" && "$COLLECTED" -gt 0 ]]; then
    ok "collected a non-empty quality list ($COLLECTED file(s))"
else
    bad "collected list is empty or unreadable (got '${COLLECTED:-<none>}')"
fi

echo ""
echo "T2: the collected list is NARROW — the gate never rewrites the GL wrapper"
# Both files carry the identical misformatted edit. The plain one is the
# positive control: without it, "gl_wrap untouched" would also be the reading
# if the shim formatted nothing at all.
REPO="$TMPROOT/t2"; mkdir -p "$REPO"; make_fixture "$REPO"
for f in "$REPO/engine/include/irreden/plain.hpp" \
         "$REPO/engine/render/include/irreden/render/gl_wrap/wrapped.hpp"; do
    printf '#pragma once\nint a(int x) { return x + 1; }\n%s\n' "$BAD_B" > "$f"
done
run_shim "$REPO" >/dev/null
assert_absent "$(cat "$REPO/engine/include/irreden/plain.hpp")" "$BAD_B" \
    "control: a normal engine header's changed line IS reformatted"
assert_contains \
    "$(cat "$REPO/engine/render/include/irreden/render/gl_wrap/wrapped.hpp")" \
    "$BAD_B" \
    "gl_wrap/ is left alone — bare collect, no INCLUDE_RENDER_BACKENDS"

echo ""
echo "T3: FORMAT_DIFF_BASE beats the @{upstream} probe and moves the LINE range"
# Two commits, each adding a separately misformatted line to the same file.
# With the base at commit B only C's line is in range; with the derived base
# (origin/master, at the init commit) both are. Same file either way, so a
# parameterization that reached only the three-dot file-list range would be
# indistinguishable here — the assertion is which LINES survive.
REPO="$TMPROOT/t3"; mkdir -p "$REPO"; make_fixture "$REPO"
TARGET="$REPO/engine/include/irreden/plain.hpp"
printf '#pragma once\nint a(int x) { return x + 1; }\n%s\n' "$BAD_B" > "$TARGET"
git -C "$REPO" commit -qam "B: add a misformatted line"
B_SHA=$(git -C "$REPO" rev-parse HEAD)
printf '#pragma once\nint a(int x) { return x + 1; }\n%s\n%s\n' "$BAD_B" "$BAD_C" > "$TARGET"
git -C "$REPO" commit -qam "C: add another misformatted line"
# An upstream that resolves to the init commit, so the probe has a real answer
# to give — the override has to be tested BEFORE it, not after.
git -C "$REPO" branch -q up refs/remotes/origin/master
git -C "$REPO" branch -q --set-upstream-to=up >/dev/null 2>&1
UPSTREAM_OK=$(git -C "$REPO" rev-parse --abbrev-ref '@{upstream}' 2>/dev/null || true)
assert_eq "$UPSTREAM_OK" "up" "fixture really does carry an upstream to be ignored"

OUT=$(run_shim "$REPO" -DFORMAT_DIFF_BASE="$B_SHA")
BODY=$(cat "$TARGET")
assert_contains "$OUT" "vs $B_SHA" "runs against the supplied base, not @{upstream}"
assert_absent "$BODY" "$BAD_C" "the line changed since the supplied base IS formatted"
assert_contains "$BODY" "$BAD_B" \
    "the line changed BEFORE the supplied base is left alone (line range moved too)"

echo ""
echo "T3b: control — the same tree with the derived base formats both lines"
REPO="$TMPROOT/t3b"; mkdir -p "$REPO"; make_fixture "$REPO"
TARGET="$REPO/engine/include/irreden/plain.hpp"
printf '#pragma once\nint a(int x) { return x + 1; }\n%s\n' "$BAD_B" > "$TARGET"
git -C "$REPO" commit -qam "B: add a misformatted line"
printf '#pragma once\nint a(int x) { return x + 1; }\n%s\n%s\n' "$BAD_B" "$BAD_C" > "$TARGET"
git -C "$REPO" commit -qam "C: add another misformatted line"
run_shim "$REPO" >/dev/null
BODY=$(cat "$TARGET")
assert_absent "$BODY" "$BAD_B" "without the override both lines are in range (B formatted)"
assert_absent "$BODY" "$BAD_C" "without the override both lines are in range (C formatted)"

echo ""
echo "T4: an unresolvable FORMAT_DIFF_BASE is fatal, not a silent clean pass"
REPO="$TMPROOT/t4"; mkdir -p "$REPO"; make_fixture "$REPO"
TARGET="$REPO/engine/include/irreden/plain.hpp"
printf '#pragma once\nint a(int x) { return x + 1; }\n%s\n' "$BAD_B" > "$TARGET"
OUT=$(run_shim "$REPO" -DFORMAT_DIFF_BASE=0000000000000000000000000000000000000000)
RC=$?
assert_contains "$OUT" "FORMAT_DIFF_BASE does not resolve" \
    "names the unresolvable base"
if [[ "$RC" -ne 0 ]]; then
    ok "exits non-zero (a bad base cannot read as 'nothing to format')"
else
    bad "exited 0 on an unresolvable base — the gate would go green having looked at nothing"
fi
assert_contains "$(cat "$TARGET")" "$BAD_B" "and formats nothing on the way out"

summarize "changed-lines clang-format standalone entry point (#3187)"

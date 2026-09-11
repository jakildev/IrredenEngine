#!/usr/bin/env bash
# Tests for the format-changed diff root (#2675).
#
# The subject is cmake/ir_quality_tools.cmake — irreden_add_quality_targets
# and the _irreden_resolve_format_root helper it calls. Like its sibling
# test_format_changed_line_scoping.sh, the subject lives outside scripts/ but
# is reached exclusively through `fleet-build --target format-changed`, and
# this directory holds the only shell-test harness in the repo.
#
# The property these cases lock: on a downstream-creation build, the tree
# format-changed diffs is the CREATION worktree, not the engine tree that owns
# the CMake project. Such a worktree has no CMake presets of its own, so
# ir-build configures it through the enclosing engine with
# -DIRREDEN_USER_PROJECTS=<worktree> — which makes PROJECT_SOURCE_DIR the
# engine while the tree being worked on is the creation's. Keying the git
# queries on the former diffs a repo nobody edited and reports "no diff vs
# origin/master; nothing to format." at exit 0: a clean answer to the wrong
# question, which is indistinguishable from a genuinely clean tree. That
# equivalence is what T2 pins, and why every zero-result line asserted here
# carries an examined-file count. See #2675.
#
# These cases configure a throwaway `project(... NONE)` root that calls the
# real irreden_add_quality_targets() and then BUILD the real format-changed
# target, so the -DFORMAT_ROOT= / -DQUALITY_FILE_LIST= wiring is exercised as
# written rather than restated here. A NONE project needs no compiler, so the
# configure is cheap and pulls no dependency graph.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
QUALITY_TOOLS="$SCRIPT_DIR/cmake/ir_quality_tools.cmake"
CHANGED_SCRIPT="$SCRIPT_DIR/cmake/run_clang_format_changed.cmake"
STYLE_FILE="$SCRIPT_DIR/.clang-format"

for f in "$QUALITY_TOOLS" "$CHANGED_SCRIPT" "$STYLE_FILE"; do
    if [[ ! -f "$f" ]]; then
        echo "SKIP: subject under test not found at $f" >&2
        exit 3  # skip status — run_all.sh must not count this as a pass (#2786)
    fi
done
for tool in cmake clang-format git; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "SKIP: $tool not available" >&2
        exit 0
    fi
done

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

ENGINE="$TMPROOT/engine"
CREATION="$TMPROOT/creation"

# Misformatted on purpose: whether a run actually reached a file is then
# visible in the file's own text, not only in the run's own status line.
write_source() {
    cat > "$1" <<'EOF'
#include <vector>
// MARKER
int  a( int x ){return x   +1;}
EOF
}

init_repo() {
    local repo="$1"
    git -C "$repo" init -q .
    git -C "$repo" config user.email t@example.com
    git -C "$repo" config user.name test
    git -C "$repo" add -A
    git -C "$repo" commit -qm init
    git -C "$repo" update-ref refs/remotes/origin/master HEAD
}

# A probe engine root: the real cmake/ directory plus the minimal root
# CMakeLists that the tail of the real one does — include the quality tools,
# call irreden_add_quality_targets(). The real root also add_subdirectory()s
# each user project; that is irrelevant here (format-changed never compiles
# anything) and would demand a real target from the creation.
mkdir -p "$ENGINE/engine"
cp -R "$SCRIPT_DIR/cmake" "$ENGINE/cmake"
cp "$STYLE_FILE" "$ENGINE/.clang-format"
write_source "$ENGINE/engine/engine_src.cpp"
cat > "$ENGINE/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(FormatRootProbe NONE)
set(IRREDEN_USER_PROJECTS "" CACHE STRING "user projects")
include(${PROJECT_SOURCE_DIR}/cmake/ir_quality_tools.cmake)
irreden_add_quality_targets()
EOF
init_repo "$ENGINE"

# A probe creation worktree: its own git repo, its own top-level layout (no
# engine/ or creations/ subtree — that is what makes the engine's named search
# roots useless against it), and the CMakeLists.txt that marks it a real user
# project.
mkdir -p "$CREATION/src"
printf '# probe creation\n' > "$CREATION/CMakeLists.txt"
# clang-format --style=file searches UPWARD from each file, and a throwaway
# tmpdir has no engine checkout above it to find one in. Without this the
# creation arm would silently format against clang-format's LLVM fallback
# while the engine arm used the repo style — two arms, two styles, and any
# assertion comparing them reads as a defect in the code under test.
cp "$STYLE_FILE" "$CREATION/.clang-format"
write_source "$CREATION/src/creation_src.cpp"
init_repo "$CREATION"

# configure <build-dir> [user-project] — echoes the configure output.
configure() {
    local bdir="$1" user_proj="${2:-}"
    cmake -S "$ENGINE" -B "$bdir" -DIRREDEN_USER_PROJECTS="$user_proj" 2>&1
}

run_target() {
    cmake --build "$1" --target format-changed 2>&1
}

# The edit each case makes: replaces the MARKER comment with a misformatted
# line. The edited line itself has to be misformatted for a reformat to be
# observable at all — format-changed is scoped to CHANGED lines (#2719), so
# editing an already-clean line correctly leaves the file's other drift alone
# and would make a "file is now clean" assertion vacuously red.
# The repo style wraps the body, so the edited line becomes several — assert
# on its head rather than the whole reformatted statement.
EDIT_RAW='int  z( int x ){return x   +9;}'
EDIT_FORMATTED='int z(int x) {'

edit_marker() {
    sed -i.bak "s|// MARKER|$EDIT_RAW|" "$1" && rm -f "$1.bak"
}

assert_edit_formatted() {
    assert_contains "$(cat "$1")" "$EDIT_FORMATTED" "$2"
}

assert_edit_untouched() {
    assert_contains "$(cat "$1")" "$EDIT_RAW" "$2"
}

restore() { git -C "$1" checkout -q -- .; }

BUILD_WITH="$TMPROOT/build-creation"
BUILD_ENGINE_ONLY="$TMPROOT/build-engine"

echo "T0: a creation-attached configure announces the creation as the diff root"
CONF=$(configure "$BUILD_WITH" "$CREATION")
assert_contains "$CONF" "format-changed diff root: $CREATION" \
    "configure names the user project as the format-changed diff root"

echo ""
echo "T1: a dirty creation worktree is what format-changed diffs (the #2675 defect)"
edit_marker "$CREATION/src/creation_src.cpp"
OUT=$(run_target "$BUILD_WITH")
# Pre-fix this printed "no diff vs origin/master in <engine>; nothing to
# format." — the whole bug in one line.
assert_absent "$OUT" "nothing to format" \
    "does not report the creation's changes as nothing to format"
assert_contains "$OUT" "formatted 1 file(s)" \
    "reports a positive formatted count for the creation's changed file"
assert_edit_formatted "$CREATION/src/creation_src.cpp" \
    "the creation's changed line is actually reformatted"
# ...and the rewrite stays line-scoped, as the engine-rooted path always was.
assert_contains "$(cat "$CREATION/src/creation_src.cpp")" 'int  a( int x ){return x   +1;}' \
    "the creation file's untouched drift line is left alone"

echo ""
echo "T2: a clean creation with a dirty ENGINE does not cross-report"
restore "$CREATION"
edit_marker "$ENGINE/engine/engine_src.cpp"
OUT=$(run_target "$BUILD_WITH")
assert_contains "$OUT" "no diff vs" "reports no diff for the clean creation tree"
assert_contains "$OUT" "0 changed file(s) examined" \
    "the clean result states its examined count, so it is not a bare 'clean'"
assert_contains "$OUT" "$CREATION" "the no-diff line names the creation root, not the engine"
assert_edit_untouched "$ENGINE/engine/engine_src.cpp" \
    "the dirty engine file is left untouched by the creation-rooted run"

echo ""
echo "T3: an engine-rooted build still formats the engine (old behavior preserved)"
# The engine file is still dirty from T2 — that is this case's input.
configure "$BUILD_ENGINE_ONLY" >/dev/null
OUT=$(run_target "$BUILD_ENGINE_ONLY")
assert_absent "$OUT" "nothing to format" "engine-rooted run sees the engine's own diff"
assert_edit_formatted "$ENGINE/engine/engine_src.cpp" \
    "the engine's changed line is reformatted"
assert_eq "$(git -C "$CREATION" status --porcelain)" "" \
    "the creation tree (restored in T2) is not disturbed by the engine-rooted run"

summarize "format-changed diff root (#2675)"

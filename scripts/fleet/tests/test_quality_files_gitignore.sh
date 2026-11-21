#!/usr/bin/env bash
# Tests for irreden_collect_quality_files' file listing and for the lists the
# quality targets read.
#
# The function (cmake/ir_quality_tools.cmake) takes its file set from the
# repository that owns each search root — tracked files plus untracked files
# git does not ignore — so a gitignored nested checkout under a search root (a
# private creation's own repo, or an agent worktree inside one), a build tree
# or a Python virtual environment never reaches QUALITY_FILES, and a file
# appearing in one never re-configures the build.
#
# The first block configures a throwaway CMake project against a fixture git
# repo (real search-root layout, a .gitignore modeled on the engine's own
# creations/* rule, and a nested .claude/worktrees/ dir) and asserts the
# gitignored entries never reach the collected list while tracked-equivalent
# entries do. The fixture's sources are untracked on purpose: the listing has
# to reach an uncommitted new file, so nothing there needs an add/commit.
#
# The second block configures a build that calls the real
# irreden_add_quality_targets() and builds the real quality-file-lists target
# (cmake/write_quality_file_lists.cmake), which is what keeps the lists
# current between configures.

set -euo pipefail

# Every path below is embedded in a generated CMakeLists.txt or matched
# against cmake's own output, and native-Windows cmake neither opens nor
# prints the MSYS `/c/...` spelling `pwd` yields there. `cygpath -m` gives
# the native `C:/...` form; hosts without cygpath keep the POSIX path.
native_path() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
    else
        printf '%s\n' "$1"
    fi
}

REPO_ROOT=$(native_path "$(cd "$(dirname "$0")/../../.." && pwd)")
QUALITY_CMAKE="$REPO_ROOT/cmake/ir_quality_tools.cmake"

if [[ ! -f "$QUALITY_CMAKE" ]]; then
    echo "test setup: cmake module not found at $QUALITY_CMAKE" >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "test setup: cmake not found on PATH" >&2
    exit 1
fi
if ! command -v git >/dev/null 2>&1; then
    echo "test setup: git not found on PATH" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

WORKDIR=$(native_path "$(mktemp -d)")
trap 'rm -rf "$WORKDIR"' EXIT

# --- Fixture repo -------------------------------------------------------
FIXTURE="$WORKDIR/fixture"
mkdir -p \
    "$FIXTURE/engine/foo" \
    "$FIXTURE/engine/render/include/irreden/render/gl_wrap" \
    "$FIXTURE/creations/demos/bar" \
    "$FIXTURE/creations/game/.claude/worktrees/pool-1" \
    "$FIXTURE/test" \
    "$FIXTURE/tools"

git -C "$FIXTURE" init -q

# Mirrors the engine's own .gitignore: creations/* is ignored except the
# demos subtree, which is re-included.
cat > "$FIXTURE/.gitignore" <<'GITIGNORE'
creations/*
!creations/demos/
!creations/demos/**
GITIGNORE

touch "$FIXTURE/engine/foo/real.hpp"
touch "$FIXTURE/engine/render/include/irreden/render/gl_wrap/GL.h"
touch "$FIXTURE/creations/demos/bar/demo.hpp"
touch "$FIXTURE/creations/game/private.hpp"
touch "$FIXTURE/creations/game/.claude/worktrees/pool-1/nested.hpp"

# --- Driver project — configures against the fixture, calls the real
#     function, and prints what it collected -----------------------------
DRIVER="$WORKDIR/driver"
mkdir -p "$DRIVER"
cat > "$DRIVER/CMakeLists.txt" <<DRIVER_EOF
cmake_minimum_required(VERSION 3.20)
project(irreden_quality_files_test NONE)
set(PROJECT_SOURCE_DIR "$FIXTURE")
include("$QUALITY_CMAKE")
irreden_collect_quality_files(result)
foreach(f IN LISTS result)
    message("FOUND: \${f}")
endforeach()
irreden_collect_quality_files(wide_result INCLUDE_RENDER_BACKENDS)
foreach(f IN LISTS wide_result)
    message("WIDE: \${f}")
endforeach()
DRIVER_EOF

OUT=$(cmake -S "$DRIVER" -B "$WORKDIR/build" 2>&1) || {
    echo "test setup: cmake configure failed:" >&2
    echo "$OUT" >&2
    exit 1
}

assert_contains "$OUT" "FOUND: $FIXTURE/engine/foo/real.hpp" \
    "tracked engine-root file is collected"
assert_contains "$OUT" "FOUND: $FIXTURE/creations/demos/bar/demo.hpp" \
    "re-included creations/demos file is collected"
assert_absent "$OUT" "FOUND: $FIXTURE/creations/game/private.hpp" \
    "gitignored creation content is dropped"
assert_absent "$OUT" "FOUND: $FIXTURE/creations/game/.claude/worktrees/pool-1/nested.hpp" \
    "nested agent worktree content is dropped"

# The listing is shared by BOTH lists irreden_collect_quality_files produces,
# so the ignore rules apply to both — including the
# wider INCLUDE_RENDER_BACKENDS one that backs header-checks. That list is a
# correctness gate, not a style one, so it needs its own lock: without these
# assertions, the filter could be made conditional on the narrow arm alone
# and still pass this suite.
assert_absent "$OUT" "FOUND: $FIXTURE/engine/render/include/irreden/render/gl_wrap/GL.h" \
    "generated GL wrapper stays out of the style-tool list"
assert_contains "$OUT" "WIDE: $FIXTURE/engine/render/include/irreden/render/gl_wrap/GL.h" \
    "INCLUDE_RENDER_BACKENDS widens the list back over the GL wrapper (#2815)"
assert_absent "$OUT" "WIDE: $FIXTURE/creations/game/private.hpp" \
    "gitignore filtering also applies to the widened header-check list"

# --- A tracked file deleted from the work tree ----------------------------
# The index still names it; a list entry that is not on disk is a file every
# consumer then fails to open.
touch "$FIXTURE/engine/foo/deleted.hpp"
git -C "$FIXTURE" add engine/foo/deleted.hpp
rm "$FIXTURE/engine/foo/deleted.hpp"
OUT=$(cmake -S "$DRIVER" -B "$WORKDIR/build" 2>&1)
assert_absent "$OUT" "FOUND: $FIXTURE/engine/foo/deleted.hpp" \
    "a tracked file deleted from the work tree is not listed"
assert_contains "$OUT" "FOUND: $FIXTURE/engine/foo/real.hpp" \
    "the deleted entry does not cost the listing its other files"

# --- A root outside any git work tree -------------------------------------
# No repository means no ignore list; the collection must still find the
# sources (a source archive is a supported way to hold the tree).
PLAIN="$WORKDIR/plain"
mkdir -p "$PLAIN/engine/foo"
touch "$PLAIN/engine/foo/archive.hpp"
PLAIN_DRIVER="$WORKDIR/plain-driver"
mkdir -p "$PLAIN_DRIVER"
cat > "$PLAIN_DRIVER/CMakeLists.txt" <<DRIVER_EOF
cmake_minimum_required(VERSION 3.20)
project(irreden_quality_files_plain_test NONE)
set(PROJECT_SOURCE_DIR "$PLAIN")
include("$QUALITY_CMAKE")
irreden_collect_quality_files(result)
foreach(f IN LISTS result)
    message("FOUND: \${f}")
endforeach()
DRIVER_EOF
# GIT_CEILING_DIRECTORIES keeps a repository above the temp dir, if the host
# has one, from adopting the fixture.
OUT=$(GIT_CEILING_DIRECTORIES="$WORKDIR" cmake -S "$PLAIN_DRIVER" -B "$WORKDIR/plain-build" 2>&1)
assert_contains "$OUT" "FOUND: $PLAIN/engine/foo/archive.hpp" \
    "a root outside any git work tree is still collected (glob fallback)"

# --- The lists the quality targets read -----------------------------------
# A user project is its own repository with its own ignore rules; the build is
# the engine's, attached through IRREDEN_USER_PROJECTS.
WRITE_LISTS="$REPO_ROOT/cmake/write_quality_file_lists.cmake"
if [[ ! -f "$WRITE_LISTS" ]]; then
    echo "test setup: list writer not found at $WRITE_LISTS" >&2
    exit 1
fi
cp -R "$REPO_ROOT/cmake" "$FIXTURE/cmake"

USER_PROJECT="$WORKDIR/user-project"
mkdir -p "$USER_PROJECT/src"
git -C "$USER_PROJECT" init -q
printf '.venv/\n' > "$USER_PROJECT/.gitignore"
printf '# probe user project\n' > "$USER_PROJECT/CMakeLists.txt"
touch "$USER_PROJECT/src/app.cpp"

TARGETS_DRIVER="$WORKDIR/targets-driver"
mkdir -p "$TARGETS_DRIVER"
cat > "$TARGETS_DRIVER/CMakeLists.txt" <<DRIVER_EOF
cmake_minimum_required(VERSION 3.20)
project(irreden_quality_targets_test NONE)
set(PROJECT_SOURCE_DIR "$FIXTURE")
set(IRREDEN_USER_PROJECTS "$USER_PROJECT")
include("$FIXTURE/cmake/ir_quality_tools.cmake")
irreden_add_quality_targets()
DRIVER_EOF

TARGETS_BUILD="$WORKDIR/targets-build"
OUT=$(cmake -S "$TARGETS_DRIVER" -B "$TARGETS_BUILD" 2>&1) || {
    echo "test setup: cmake configure of the targets driver failed:" >&2
    echo "$OUT" >&2
    exit 1
}
STYLE_LIST="$TARGETS_BUILD/irreden_quality_files.cmake"
USER_LIST="$TARGETS_BUILD/irreden_format_changed_files.cmake"
assert_contains "$(cat "$STYLE_LIST")" "$FIXTURE/engine/foo/real.hpp" \
    "the configure writes the style-tool list"
assert_contains "$(cat "$USER_LIST")" "$USER_PROJECT/src/app.cpp" \
    "the configure writes the user-project list"

# CMake writes VerifyGlobs.cmake only for a build holding a CONFIGURE_DEPENDS
# glob, and that file is what re-runs the configure when a glob's result
# changes. Its absence is the property: no file appearing under a search root
# can force a re-configure.
if [[ -e "$TARGETS_BUILD/CMakeFiles/VerifyGlobs.cmake" ]]; then
    bad "the build registers no configure-time glob dependency (VerifyGlobs.cmake present)"
else
    ok "the build registers no configure-time glob dependency"
fi

# What installing a package with C/C++ headers into a virtual environment
# does, plus one ordinary new source in each tree.
mkdir -p "$USER_PROJECT/.venv/lib/site-packages/pkg/include"
touch "$USER_PROJECT/.venv/lib/site-packages/pkg/include/vendored.h"
touch "$USER_PROJECT/src/added.cpp"
touch "$FIXTURE/engine/foo/added.hpp"

OUT=$(cmake --build "$TARGETS_BUILD" --target quality-file-lists 2>&1) || {
    bad "the quality-file-lists target builds"
    echo "$OUT" >&2
}
assert_absent "$OUT" "Configuring done" \
    "files added under the search roots do not re-configure the build"
assert_contains "$(cat "$STYLE_LIST")" "$FIXTURE/engine/foo/added.hpp" \
    "an engine source added after the configure is on the refreshed list"
assert_contains "$(cat "$USER_LIST")" "$USER_PROJECT/src/added.cpp" \
    "a user-project source added after the configure is on the refreshed list"
assert_absent "$(cat "$USER_LIST")" ".venv" \
    "a file under a directory the user project ignores stays off its list"

# Every quality target has to refresh the lists, not only a direct build of
# the refresh target: header-checks is the one that needs no external tool.
touch "$FIXTURE/engine/foo/added_later.hpp"
OUT=$(cmake --build "$TARGETS_BUILD" --target header-checks 2>&1) || true
assert_contains "$(cat "$TARGETS_BUILD/irreden_header_check_files.cmake")" \
    "$FIXTURE/engine/foo/added_later.hpp" \
    "building a quality target refreshes the lists first"

summarize "irreden_collect_quality_files listing and quality-target lists"

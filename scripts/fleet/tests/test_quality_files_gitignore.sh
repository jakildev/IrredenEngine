#!/usr/bin/env bash
# Tests for irreden_collect_quality_files' gitignore filtering (#2791).
#
# The function (cmake/ir_quality_tools.cmake) walks four search roots with
# file(GLOB_RECURSE) — a filesystem walk, not a repository walk. Before this
# fix nothing dropped content the engine repo's own .gitignore excludes, so
# a gitignored nested checkout under a search root (a private creation's own
# repo, or an agent worktree inside one) was swept into QUALITY_FILES: the
# `header-checks` / `lint` targets reported violations no one could fix from
# the flagged PR, and `format` rewrote files live in another agent's
# in-progress worktree.
#
# This suite configures a throwaway CMake project against a fixture git repo
# (real search-root layout, a .gitignore modeled on the engine's own
# creations/* rule, and a nested .claude/worktrees/ dir) and asserts the
# gitignored entries never reach the collected list while tracked-equivalent
# entries do.
#
# The fixture repo has no commits — git check-ignore evaluates pathname
# rules only, not tracked status, so an uncommitted .gitignore is sufficient
# and keeps the fixture setup to a `git init` with no add/commit.

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
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

WORKDIR="$(mktemp -d)"
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

# The filter runs inside irreden_collect_quality_files, below the reject
# chain, so it applies to BOTH lists the function produces — including the
# wider INCLUDE_RENDER_BACKENDS one that backs header-checks (#2815). That
# list is a correctness gate, not a style one, so it needs its own lock:
# without these, the filter could be made conditional on the narrow arm and
# every assertion above would still pass.
assert_absent "$OUT" "FOUND: $FIXTURE/engine/render/include/irreden/render/gl_wrap/GL.h" \
    "generated GL wrapper stays out of the style-tool list"
assert_contains "$OUT" "WIDE: $FIXTURE/engine/render/include/irreden/render/gl_wrap/GL.h" \
    "INCLUDE_RENDER_BACKENDS widens the list back over the GL wrapper (#2815)"
assert_absent "$OUT" "WIDE: $FIXTURE/creations/game/private.hpp" \
    "gitignore filtering also applies to the widened header-check list"

summarize "irreden_collect_quality_files gitignore filtering"

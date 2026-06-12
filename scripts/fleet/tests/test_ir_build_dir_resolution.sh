#!/usr/bin/env bash
# Tests for the ir-build / ir-run build-dir resolution helpers
# (engine/tools/lib/concurrency_helpers.sh).
#
# #1669 made downstream-creation worktree builds first-class: a git repo
# rooted at creations/<name>/.claude/worktrees/<agent>/ has no CMake
# presets of its own, so its build routes to the enclosing engine root at
# build-<creation>-<agent>/ and ir-build auto-configures that dir with
# -DIRREDEN_USER_PROJECTS=<worktree>. These tests pin the pure path
# resolution (ir_enclosing_engine_root / ir_creation_worktree_engine_root /
# ir_default_build_dir) against a fake directory layout — no git, no cmake.
#
# Covers:
#   - engine checkout (presets at root) → <root>/build (unchanged)
#   - engine nested worktree (own presets) → <worktree>/build (unchanged)
#   - creation worktree under <engine>/creations/<name>/.claude/worktrees/
#     <agent>/ → <engine>/build-<name>-<agent> (the #1669 behavior)
#   - repo outside any engine tree → <root>/build (unchanged)
#   - presets-less dir under the engine but NOT under creations/ →
#     <root>/build (no false-positive creation detection)
#   - ir_enclosing_engine_root walk-up and miss cases

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
HELPERS="$SCRIPT_DIR/engine/tools/lib/concurrency_helpers.sh"

if [[ ! -f "$HELPERS" ]]; then
    echo "test setup: helpers not found at $HELPERS" >&2
    exit 1
fi
# Isolate the helpers' lock-dir side effects from the host's live locks.
IR_LOCK_ROOT="$(mktemp -d)/locks"
export IR_LOCK_ROOT
# shellcheck source=../../../engine/tools/lib/concurrency_helpers.sh
source "$HELPERS"

PASS=0
FAIL=0

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

assert_rc() {
    local rc="$1" expected="$2" msg="$3"
    assert_eq "$rc" "$expected" "$msg"
}

# --- Fake layout -------------------------------------------------------------
FAKE="$(mktemp -d)"
trap 'rm -rf "$FAKE" "${IR_LOCK_ROOT%/locks}"' EXIT

ENG="$FAKE/eng"
mkdir -p "$ENG/engine"
touch "$ENG/CMakePresets.json"

ENG_WT="$ENG/.claude/worktrees/opus-worker-1"
mkdir -p "$ENG_WT/engine"
touch "$ENG_WT/CMakePresets.json"

CREATION_WT="$ENG/creations/game/.claude/worktrees/agent-1"
mkdir -p "$CREATION_WT"

NON_CREATION="$ENG/.fleet/scratch-repo"
mkdir -p "$NON_CREATION"

OUTSIDE="$FAKE/elsewhere/repo"
mkdir -p "$OUTSIDE"

# --- T1: ir_enclosing_engine_root --------------------------------------------
echo "T1: ir_enclosing_engine_root walk-up"
assert_eq "$(ir_enclosing_engine_root "$CREATION_WT")" "$ENG" \
    "creation worktree walks up to the engine root"
assert_eq "$(ir_enclosing_engine_root "$ENG")" "$ENG" \
    "engine root resolves to itself"
rc=0; ir_enclosing_engine_root "$OUTSIDE" >/dev/null || rc=$?
assert_rc "$rc" 1 "dir outside any engine tree returns 1"

# --- T2: ir_creation_worktree_engine_root ------------------------------------
echo "T2: ir_creation_worktree_engine_root detection"
assert_eq "$(ir_creation_worktree_engine_root "$CREATION_WT")" "$ENG" \
    "creation worktree detected, echoes enclosing engine root"
rc=0; ir_creation_worktree_engine_root "$ENG" >/dev/null || rc=$?
assert_rc "$rc" 1 "engine root (has presets) is not a creation worktree"
rc=0; ir_creation_worktree_engine_root "$ENG_WT" >/dev/null || rc=$?
assert_rc "$rc" 1 "engine nested worktree (own presets) is not a creation worktree"
rc=0; ir_creation_worktree_engine_root "$NON_CREATION" >/dev/null || rc=$?
assert_rc "$rc" 1 "presets-less repo under the engine but outside creations/ is not detected"
rc=0; ir_creation_worktree_engine_root "$OUTSIDE" >/dev/null || rc=$?
assert_rc "$rc" 1 "repo outside the engine tree is not detected"

# --- T3: ir_default_build_dir ------------------------------------------------
echo "T3: ir_default_build_dir routing"
assert_eq "$(ir_default_build_dir "$ENG")" "$ENG/build" \
    "engine checkout builds in-tree"
assert_eq "$(ir_default_build_dir "$ENG_WT")" "$ENG_WT/build" \
    "engine nested worktree builds in-tree"
assert_eq "$(ir_default_build_dir "$CREATION_WT")" "$ENG/build-game-agent-1" \
    "creation worktree routes to <engine>/build-<creation>-<agent>"
assert_eq "$(ir_default_build_dir "$NON_CREATION")" "$NON_CREATION/build" \
    "non-creation repo under the engine builds in-tree"
assert_eq "$(ir_default_build_dir "$OUTSIDE")" "$OUTSIDE/build" \
    "repo outside the engine tree builds in-tree"

# --- Summary ------------------------------------------------------------------
echo
echo "pass: $PASS  fail: $FAIL"
(( FAIL == 0 ))

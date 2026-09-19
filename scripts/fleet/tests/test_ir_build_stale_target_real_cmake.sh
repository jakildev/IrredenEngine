#!/usr/bin/env bash
# Positive-fire suite for ir-build's post-failure retry (engine/tools/bin/
# ir-build) against the REAL detection: real cmake, real make, no stub. The
# hermetic call-sequence truth table lives in
# test_ir_build_stale_target_retry.sh; this suite proves the mechanism
# actually regenerates a real Makefile tree and rebuilds against it.
#
# Fixture: a `project(probe NONE)` tree (no compiler) with an
# add_custom_target per fixture target, configured once, then a SECOND
# target appended to CMakeLists.txt after that configure — reproducing a
# branch switch that add_subdirectory's a new demo. `ir-build --target
# <the new target>` must regenerate and succeed with no manual cmake call.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
# shellcheck source=lib_preflight.sh
source "$(dirname "$0")/lib_preflight.sh"
IR_BUILD="$SCRIPT_DIR/engine/tools/bin/ir-build"

if [[ ! -x "$IR_BUILD" ]]; then
    echo "SKIP: $IR_BUILD not found or not executable" >&2
    exit 3
fi

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) GENERATOR="MinGW Makefiles"; BUILD_TOOL="mingw32-make" ;;
    *)                    GENERATOR="Unix Makefiles";  BUILD_TOOL="make" ;;
esac

if ! command -v cmake >/dev/null 2>&1 || ! command -v "$BUILD_TOOL" >/dev/null 2>&1; then
    echo "SKIP: cmake or $BUILD_TOOL not on PATH" >&2
    exit 3
fi

# Isolate the helpers' lock-dir side effects from the host's live locks —
# ir-build's own concurrency_helpers.sh honors IR_LOCK_ROOT when set.
IR_LOCK_ROOT="$(mktemp -d)/locks"
export IR_LOCK_ROOT

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
fi

FIXTURE="$(mktemp -d)"
trap 'rm -rf "$FIXTURE" "${IR_LOCK_ROOT%/locks}"' EXIT

# git-inited so ir_worktree_root resolves HERE (the T6 pattern in
# test_ir_build_dir_resolution.sh) rather than walking up past a real
# engine checkout that happens to be an ancestor of $TMPDIR.
git -C "$FIXTURE" init -q

cat > "$FIXTURE/CMakeLists.txt" <<'CMAKE_EOF'
cmake_minimum_required(VERSION 3.16)
project(probe NONE)
add_custom_target(alpha ALL COMMAND ${CMAKE_COMMAND} -E touch alpha.stamp)
CMAKE_EOF

echo "T1: initial configure at the fixture's first commit (no ir-build involved yet)"
run() {
    if [[ -n "$TIMEOUT_CMD" ]]; then
        "$TIMEOUT_CMD" 60 "$@"
    else
        "$@"
    fi
}
CONFIGURE_LOG="$FIXTURE/configure.log"
rc=0
run cmake -S "$FIXTURE" -B "$FIXTURE/build" -G "$GENERATOR" >"$CONFIGURE_LOG" 2>&1 || rc=$?
assert_eq "$rc" "0" "T1: initial configure succeeds"
assert_eq "$([[ -f "$FIXTURE/build/CMakeCache.txt" ]] && echo yes || echo no)" "yes" \
    "T1: build dir is configured"

# Append the SECOND target — after the configure above, so the build dir has
# never seen it. This is the repro shape: a branch switch that
# add_subdirectory's a new demo edits a CMakeLists.txt the tree already
# tracks, without anyone re-running cmake by hand.
cat >> "$FIXTURE/CMakeLists.txt" <<'CMAKE_EOF'
add_custom_target(beta ALL COMMAND ${CMAKE_COMMAND} -E touch beta.stamp)
CMAKE_EOF

echo "T2: ir-build --target beta regenerates and succeeds with no manual cmake call"
BETA_LOG="$FIXTURE/ir-build-beta.log"
rc=0
(
    cd "$FIXTURE" || exit 127
    if [[ -n "$TIMEOUT_CMD" ]]; then
        "$TIMEOUT_CMD" 60 "$IR_BUILD" --target beta
    else
        "$IR_BUILD" --target beta
    fi
) >"$BETA_LOG" 2>&1 || rc=$?
assert_eq "$rc" "0" "T2: ir-build --target beta succeeds"
assert_contains "$(cat "$BETA_LOG")" "retrying" "T2: ir-build announces the retry"
assert_eq "$([[ -f "$FIXTURE/build/beta.stamp" ]] && echo yes || echo no)" "yes" \
    "T2: beta.stamp exists after the retry"

echo "T3: negative control — a never-defined target fails, no retry, no stamp"
GAMMA_LOG="$FIXTURE/ir-build-gamma.log"
rc=0
(
    cd "$FIXTURE" || exit 127
    if [[ -n "$TIMEOUT_CMD" ]]; then
        "$TIMEOUT_CMD" 60 "$IR_BUILD" --target gamma
    else
        "$IR_BUILD" --target gamma
    fi
) >"$GAMMA_LOG" 2>&1 || rc=$?
assert_eq "$([[ "$rc" -ne 0 ]] && echo nonzero || echo zero)" "nonzero" \
    "T3: a never-defined target fails"
assert_eq "$([[ -f "$FIXTURE/build/gamma.stamp" ]] && echo yes || echo no)" "no" \
    "T3: gamma.stamp is never created"
assert_absent "$(cat "$GAMMA_LOG")" "retrying" \
    "T3: no retry — nothing changed since beta's regenerate"

summarize "ir-build stale-target retry tests (real cmake)"

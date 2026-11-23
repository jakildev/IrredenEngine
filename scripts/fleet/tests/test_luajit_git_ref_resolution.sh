#!/usr/bin/env bash
# Tests for engine/script/third_party/luajit/resolve_git_ref.cmake, the
# LuaJIT revision choice.
#
# Each case configures a throwaway CMake project that includes the real
# module and prints the revision it resolved. The migration cases first
# configure the build directory from a CMakeLists.txt declaring the deprecated
# IR_LUAJIT_GIT_TAG cache entry the way the LuaJIT one used to, then swap in
# the module and reconfigure that same directory, so the cache the module
# reads is one CMake itself wrote.

set -euo pipefail

# Paths are embedded in generated CMakeLists.txt files, and native-Windows
# cmake does not open the MSYS `/c/...` spelling `pwd` yields there.
native_path() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
    else
        printf '%s\n' "$1"
    fi
}

REPO_ROOT=$(native_path "$(cd "$(dirname "$0")/../../.." && pwd)")
MODULE="$REPO_ROOT/engine/script/third_party/luajit/resolve_git_ref.cmake"

if [[ ! -f "$MODULE" ]]; then
    echo "SKIP: module under test not found at $MODULE" >&2
    exit 3
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "test setup: cmake not found on PATH" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

PIN=$(sed -n 's/^set(_luajit_git_tag \([0-9a-f]\{40\}\))$/\1/p' "$MODULE")
if [[ -z "$PIN" ]]; then
    echo "test setup: no 40-hex pinned commit found in $MODULE" >&2
    exit 1
fi

WORKDIR=$(native_path "$(mktemp -d "${TMPDIR:-/tmp}/luajit-ref.XXXXXX")")
trap 'rm -rf "$WORKDIR"' EXIT

SRC="$WORKDIR/src"
mkdir -p "$SRC"

# CMake binds a build directory to one source directory, so the legacy and
# current projects take turns in the same one.
use_legacy() {
    cat > "$SRC/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.15)
project(luajit_ref LANGUAGES NONE)
set(IR_LUAJIT_GIT_TAG "v2.1" CACHE STRING
    "LuaJIT git ref (branch, tag, or commit) to fetch and build")
CMAKE
}

use_current() {
    cat > "$SRC/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.15)
project(luajit_ref LANGUAGES NONE)
include("$MODULE")
message(STATUS "RESOLVED_LUAJIT_REF=\${_luajit_git_tag}")
CMAKE
}

# configure <build> [cmake args...] — prints cmake's combined output; a
# failed configure is a setup error, not an assertion.
configure() {
    local build="$1" out
    shift
    if ! out=$(cmake -S "$SRC" -B "$build" "$@" 2>&1); then
        echo "test setup: configure in $build failed:" >&2
        echo "$out" >&2
        exit 1
    fi
    printf '%s\n' "$out"
}

resolved() {
    printf '%s\n' "$1" | sed -n 's/.*RESOLVED_LUAJIT_REF=//p'
}

cached_legacy_entry() {
    grep '^IR_LUAJIT_GIT_TAG:' "$1/CMakeCache.txt" || true
}

DEPRECATION='IR_LUAJIT_GIT_TAG is deprecated'

echo "T1: a fresh build directory builds the pin"
use_current
OUT=$(configure "$WORKDIR/b-fresh")
assert_eq "$(resolved "$OUT")" "$PIN" "no override resolves to the pinned commit"
assert_absent "$OUT" "$DEPRECATION" "no deprecation warning without the deprecated variable"

echo "T2: IR_LUAJIT_GIT_REF overrides the pin"
OUT=$(configure "$WORKDIR/b-ref" -DIR_LUAJIT_GIT_REF=v2.1)
assert_eq "$(resolved "$OUT")" "v2.1" "IR_LUAJIT_GIT_REF=v2.1 resolves to v2.1"

echo "T3: a directory holding the deprecated variable's cached default migrates to the pin"
B="$WORKDIR/b-legacy-default"
use_legacy
configure "$B" > /dev/null
assert_eq "$(cached_legacy_entry "$B")" "IR_LUAJIT_GIT_TAG:STRING=v2.1" \
    "setup: the legacy configure cached v2.1"
use_current
OUT=$(configure "$B")
assert_eq "$(resolved "$OUT")" "$PIN" "cached v2.1 resolves to the pinned commit"
assert_absent "$OUT" "$DEPRECATION" "the cached default draws no deprecation warning"
assert_eq "$(cached_legacy_entry "$B")" "" "the cached default is dropped from the cache"
OUT=$(configure "$B")
assert_eq "$(resolved "$OUT")" "$PIN" "a second reconfigure stays on the pin"

echo "T4: a directory holding a custom deprecated override keeps building it"
CUSTOM=0123456789abcdef0123456789abcdef01234567
B="$WORKDIR/b-legacy-custom"
use_legacy
configure "$B" -DIR_LUAJIT_GIT_TAG="$CUSTOM" > /dev/null
assert_eq "$(cached_legacy_entry "$B")" "IR_LUAJIT_GIT_TAG:STRING=$CUSTOM" \
    "setup: the legacy configure cached the custom ref"
use_current
OUT=$(configure "$B")
assert_eq "$(resolved "$OUT")" "$CUSTOM" "the cached custom ref is still built"
assert_contains "$OUT" "$DEPRECATION" "the custom ref draws a deprecation warning"
assert_eq "$(cached_legacy_entry "$B")" "IR_LUAJIT_GIT_TAG:STRING=$CUSTOM" \
    "the custom ref stays in the cache"
OUT=$(configure "$B")
assert_eq "$(resolved "$OUT")" "$CUSTOM" "a second reconfigure still builds the custom ref"

echo "T5: IR_LUAJIT_GIT_REF takes precedence over a custom deprecated override"
OUT=$(configure "$B" -DIR_LUAJIT_GIT_REF=v2.1)
assert_eq "$(resolved "$OUT")" "v2.1" "IR_LUAJIT_GIT_REF wins over the cached custom ref"
assert_absent "$OUT" "$DEPRECATION" "the shadowed deprecated variable draws no warning"
OUT=$(configure "$B" -DIR_LUAJIT_GIT_REF=)
assert_eq "$(resolved "$OUT")" "$CUSTOM" "clearing IR_LUAJIT_GIT_REF falls back to the custom ref"

summarize "LuaJIT git ref resolution"

#!/usr/bin/env bash
# Positive control for cmake/run_header_checks_standalone.cmake.
#
# A green header-check run proves nothing on its own: the executor could be
# scanning zero files, or matching nothing, and would look identical. These
# tests drive it against a fixture tree that contains a known violation and
# assert it FAILS, then assert the clean tree passes.
#
# Fixture-based rather than run against the real repo, so the suite stays
# hermetic and never writes into engine/.
#
# Covers:
#   - a mutable namespace-scope global in a header  → exit 1, names the file
#   - `constexpr` / `const` constants               → exit 0 (not state)
#   - a clean fixture tree                          → exit 0
#   - a missing PROJECT_ROOT                        → exit 1 (usage guard)

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
CHECKER="$SCRIPT_DIR/cmake/run_header_checks_standalone.cmake"

source "$(dirname "$0")/lib_assert.sh"

if [[ ! -f "$CHECKER" ]]; then
    echo "test setup: checker not found at $CHECKER" >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "test setup: cmake not on PATH" >&2
    exit 1
fi

TMPROOT=$(mktemp -d)
trap 'rm -rf "$TMPROOT"' EXIT

# The checker only needs cmake/ to identify a tree as the repo root, plus one
# of the globbed source roots to have headers in it.
make_fixture() {
    local root="$1"
    mkdir -p "$root/cmake" "$root/engine/include/irreden"
    cp "$SCRIPT_DIR/cmake/ir_quality_tools.cmake" \
       "$SCRIPT_DIR/cmake/run_header_convention_checks.cmake" \
       "$CHECKER" "$root/cmake/"
    cat > "$root/engine/include/irreden/clean.hpp" <<'EOF'
#pragma once
namespace IRFixture {
constexpr int kCleanConstant = 1;
const char *const kCleanName = "clean";
}
EOF
}

run_checker() {
    local root="$1"
    cmake -DPROJECT_ROOT="$root" -P "$root/cmake/run_header_checks_standalone.cmake" 2>&1
}

# --- clean tree passes ------------------------------------------------------
CLEAN="$TMPROOT/clean"
make_fixture "$CLEAN"
clean_out=$(run_checker "$CLEAN")
clean_rc=$?
assert_eq "0" "$clean_rc" "clean fixture exits 0"
assert_contains "$clean_out" "Header convention checks scanned" \
    "clean run reports what it scanned"

# A run that scans nothing would also exit 0 — pin that it saw the fixture.
assert_absent "$clean_out" "scanned 0 header file(s)" \
    "clean run actually scanned headers"

# --- a banned mutable global fails ------------------------------------------
DIRTY="$TMPROOT/dirty"
make_fixture "$DIRTY"
cat > "$DIRTY/engine/include/irreden/violation.hpp" <<'EOF'
#pragma once
namespace IRFixture {
inline int g_mutableGlobal = 0;
}
EOF
dirty_out=$(run_checker "$DIRTY")
dirty_rc=$?
assert_eq "1" "$dirty_rc" "header global makes the checker exit 1"
assert_contains "$dirty_out" "violation.hpp" "failure names the offending file"
assert_contains "$dirty_out" "g_mutableGlobal" "failure names the declaration"

# The clean fixture's constants must not be what tripped it.
assert_absent "$dirty_out" "clean.hpp" "constexpr/const constants stay allowed"

# --- usage guard ------------------------------------------------------------
noroot_out=$(cmake -P "$CHECKER" 2>&1)
noroot_rc=$?
assert_eq "1" "$noroot_rc" "missing PROJECT_ROOT exits 1"
assert_contains "$noroot_out" "PROJECT_ROOT is required" \
    "missing PROJECT_ROOT explains itself"

summarize "run_header_checks_standalone tests"

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
#   - a clean fixture tree                          → exit 0, and the Metal
#     kernel registry check ran (scanned 1 kernel; _body fragment excluded)
#   - an unregistered Metal compute kernel          → exit 1, names the kernel
#   - a registry with no bare "}" terminator line   → exit 1 (EOF guard, not
#     a silent scan past the function into unrelated string literals)
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
# of the globbed source roots to have headers in it. The Metal kernel registry
# check rides the same entry point, so the fixture also carries one registered
# kernel, one _body include-fragment (must be excluded from the scan), and a
# registry function shaped like the real metal_pipeline.cpp one.
make_fixture() {
    local root="$1"
    mkdir -p "$root/cmake" "$root/engine/include/irreden" \
             "$root/engine/render/src/shaders/metal" \
             "$root/engine/render/src/metal"
    cp "$SCRIPT_DIR/cmake/ir_quality_tools.cmake" \
       "$SCRIPT_DIR/cmake/run_header_convention_checks.cmake" \
       "$SCRIPT_DIR/cmake/run_metal_kernel_registry_check.cmake" \
       "$CHECKER" "$root/cmake/"
    cat > "$root/engine/include/irreden/clean.hpp" <<'EOF'
#pragma once
namespace IRFixture {
constexpr int kCleanConstant = 1;
const char *const kCleanName = "clean";
}
EOF
    echo '// fixture kernel' \
        > "$root/engine/render/src/shaders/metal/c_fixture_kernel.metal"
    echo '// fixture include-fragment' \
        > "$root/engine/render/src/shaders/metal/c_fixture_kernel_body.metal"
    cat > "$root/engine/render/src/metal/metal_pipeline.cpp" <<'EOF'
namespace IRRender {
namespace {

MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
    if (functionName == "c_fixture_kernel") {
        return MTL::Size(16, 16, 1);
    }
    return MTL::Size(1, 1, 1);
}

}  // namespace
}  // namespace IRRender
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

# The Metal kernel registry check must ride the same CI entry point (a checker
# wired only to the header-checks/lint targets never runs in CI), and the count
# pins the _body include-fragment exclusion.
assert_contains "$clean_out" "Metal kernel registry check scanned 1 compute kernel(s)" \
    "metal registry check runs on the CI path and excludes _body fragments"

# --- an unregistered Metal kernel fails -------------------------------------
METAL_DIRTY="$TMPROOT/metal-dirty"
make_fixture "$METAL_DIRTY"
echo '// unregistered fixture kernel' \
    > "$METAL_DIRTY/engine/render/src/shaders/metal/c_unregistered_kernel.metal"
metal_out=$(run_checker "$METAL_DIRTY")
metal_rc=$?
assert_eq "1" "$metal_rc" "unregistered Metal kernel makes the checker exit 1"
assert_contains "$metal_out" "c_unregistered_kernel" \
    "failure names the unregistered kernel"
assert_absent "$metal_out" "c_fixture_kernel" \
    "registered kernel is not flagged as missing"

# --- a registry without its bare "}" terminator fails, not false-cleans ------
# The function-body scan ends on a line that is exactly "}"; if the function
# is ever indented (namespace style change, moved into a block), the scan
# would run to EOF sweeping up unrelated string literals — which can mask a
# truly unregistered kernel. The EOF guard must turn that into a hard fail.
INDENTED="$TMPROOT/indented"
make_fixture "$INDENTED"
cat > "$INDENTED/engine/render/src/metal/metal_pipeline.cpp" <<'EOF'
namespace IRRender {
    MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
        if (functionName == "c_fixture_kernel") {
            return MTL::Size(16, 16, 1);
        }
        return MTL::Size(1, 1, 1);
    }
}  // namespace IRRender
EOF
indent_out=$(run_checker "$INDENTED")
indent_rc=$?
assert_eq "1" "$indent_rc" "indented registry (no bare \"}\" line) exits 1"
assert_contains "$indent_out" "closing-brace terminator" \
    "EOF-without-terminator failure explains itself"

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

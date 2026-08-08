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
#   - the same global under a render-backend path   → exit 1 (scope, #2889)
#   - a clean fixture tree                          → exit 0, and both Metal
#     checks ran (1 kernel scanned, _body fragment excluded as an entry point
#     but still read through the wrapper's include chain)
#   - an unregistered Metal compute kernel          → exit 1, names the kernel
#   - a registry with no bare "}" terminator line   → exit 1 (EOF guard, not
#     a silent scan past the function into unrelated string literals)
#   - a scratch consumer absent from the list       → exit 1, names the kernel
#   - a non-atomic decl at the scratch slot         → exit 0 (the #1619 params
#     UBO shape must NOT be flagged — negative control)
#   - a commented-out list entry                    → exit 1 (the runtime stops
#     binding for it, so it must read as absent, not present)
#   - a hand-wrapped scratch declaration            → exit 1 (the qualifier test
#     reads the declaration window, not the attribute's line)
#   - an atomic neighbour on the slot's line        → exit 0 (that qualifier
#     belongs to the preceding parameter — negative control)
#   - a listed kernel that declares no scratch      → exit 1 (reverse drift:
#     the sticky bind would clobber its slot, #1619)
#   - a scratch list with no bare "}" terminator    → exit 1 (EOF guard)
#   - an unresolvable `#include "…"`                → exit 1 (a dropped include
#     would shrink a consumer's scanned source into a false clean)
#   - no slot-attributed declaration anywhere       → exit 1 (the matcher broke;
#     the slot is aliased and always has consumers)
#   - an unreadable scratch-slot constant           → exit 1 (anchor guard)
#   - a missing PROJECT_ROOT                        → exit 1 (usage guard)
#   - `inline void *g_x = ...;`                     → exit 1 (#2916: the old
#     undocumented `void` reject exempted this mutable pointer)
#   - `inline void f() {}`                          → exit 0 (already caught
#     by the function-declaration guard; the `void` reject was redundant)
#   - a wrapped declaration terminator              → exit 1 (#2916: the
#     formatter-defeat case the repo's own 100-col clang-format produces)
#   - `inline const T *const p`                     → exit 0 (both ends const
#     is a program constant, and the arm pins that the header entered the scan
#     rather than passing by skipping the candidate gate)
#   - `inline const T *p` / `inline T *const p`     → exit 1 (single-sided
#     const is still unowned mutable state — #2726's `g_activeShots` shape)

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

# Both Metal checks parse metal_pipeline.cpp, so the pipeline fixture is
# parameterized: $2 is the space-separated kernel list for
# threadgroupSizeForFunctionName, $3 the list for
# functionUsesImageAtomicScratch. Arms that inject a new kernel must register
# it in the former — otherwise the registry check (#2798) FATALs first and the
# arm silently tests that checker instead of the one it names.
#
# $4 (optional) is a list of scratch entries emitted COMMENTED OUT — the way a
# developer disables one, rather than deleting it. bindComputeResources binds
# off the live list, so those names must read as absent.
write_pipeline_cpp() {
    local root="$1" registry_names="$2" scratch_names="$3" commented_names="${4:-}" name
    local out="$root/engine/render/src/metal/metal_pipeline.cpp"
    {
        echo 'namespace IRRender {'
        echo 'namespace {'
        echo
        echo 'MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {'
        for name in $registry_names; do
            echo "    if (functionName == \"$name\") {"
            echo '        return MTL::Size(16, 16, 1);'
            echo '    }'
        done
        echo '    return MTL::Size(1, 1, 1);'
        echo '}'
        echo
        echo 'bool functionUsesImageAtomicScratch(const std::string &functionName) {'
        echo '    return false'
        for name in $scratch_names; do
            echo "           || functionName == \"$name\""
        done
        for name in $commented_names; do
            echo "        // || functionName == \"$name\""
        done
        echo '        ;'
        echo '}'
        echo
        echo '}  // namespace'
        echo '}  // namespace IRRender'
    } > "$out"
}

# The checker only needs cmake/ to identify a tree as the repo root, plus one
# of the globbed source roots to have headers in it. Both Metal checks ride the
# same entry point, so the fixture also carries one registered kernel, one
# _body include-fragment, and the two registry functions shaped like the real
# metal_pipeline.cpp ones. The scratch declaration lives in the _body fragment
# on purpose: the fragment is excluded as a scan *entry point* (which the
# registry check's count pins) yet must still be read through the wrapper's
# include chain (which the scratch check's consumer count pins), and only
# splitting them across the two files tests both properties at once.
make_fixture() {
    local root="$1"
    mkdir -p "$root/cmake" "$root/engine/include/irreden" \
             "$root/engine/render/src/shaders/metal" \
             "$root/engine/render/src/metal" \
             "$root/engine/render/include/irreden/render/metal"
    cp "$SCRIPT_DIR/cmake/ir_quality_tools.cmake" \
       "$SCRIPT_DIR/cmake/run_header_convention_checks.cmake" \
       "$SCRIPT_DIR/cmake/run_metal_kernel_registry_check.cmake" \
       "$SCRIPT_DIR/cmake/run_metal_scratch_consumer_check.cmake" \
       "$CHECKER" "$root/cmake/"
    cat > "$root/engine/include/irreden/clean.hpp" <<'EOF'
#pragma once
namespace IRFixture {
constexpr int kCleanConstant = 1;
const char *const kCleanName = "clean";
}
EOF
    cat > "$root/engine/render/include/irreden/render/metal/metal_runtime.hpp" <<'EOF'
#pragma once
namespace IRRender {
constexpr std::uint32_t kMetalImageAtomicScratchSlot = 16;
}
EOF
    cat > "$root/engine/render/src/shaders/metal/c_fixture_kernel.metal" <<'EOF'
// fixture kernel
#include "c_fixture_kernel_body.metal"
EOF
    cat > "$root/engine/render/src/shaders/metal/c_fixture_kernel_body.metal" <<'EOF'
// fixture include-fragment
kernel void IR_FIXTURE_KERNEL_NAME(
    device atomic_int* distanceScratch [[buffer(16)]],
    uint3 gid [[thread_position_in_grid]]
) {}
EOF
    write_pipeline_cpp "$root" "c_fixture_kernel" "c_fixture_kernel"
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

# Same for the scratch-consumer check (#2878). The "(1 declare" half is the
# load-bearing one: the fixture's scratch declaration lives ONLY in the _body
# fragment, so a count of 1 can only come from resolving the wrapper's
# #include chain. A checker that read entry-point files alone would report 0
# here and still exit 0, since the fixture list would then agree with an empty
# expected set.
assert_contains "$clean_out" "Metal scratch-consumer check scanned 1 compute kernel(s)" \
    "metal scratch-consumer check runs on the CI path"
assert_contains "$clean_out" "(1 declare the image-atomic scratch at buffer slot 16)" \
    "scratch check resolves the wrapper -> _body include chain"

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

# --- a scratch consumer missing from the list fails --------------------------
# Registered in threadgroupSizeForFunctionName so the #2798 check passes and
# this arm actually reaches the #2878 one.
SCRATCH_MISSING="$TMPROOT/scratch-missing"
make_fixture "$SCRATCH_MISSING"
cat > "$SCRATCH_MISSING/engine/render/src/shaders/metal/c_fixture_consumer.metal" <<'EOF'
kernel void c_fixture_consumer(
    device atomic_int* distanceScratch [[buffer(16)]],
    uint3 gid [[thread_position_in_grid]]
) {}
EOF
write_pipeline_cpp "$SCRATCH_MISSING" \
    "c_fixture_kernel c_fixture_consumer" "c_fixture_kernel"
scratch_missing_out=$(run_checker "$SCRATCH_MISSING")
scratch_missing_rc=$?
assert_eq "1" "$scratch_missing_rc" "unlisted scratch consumer makes the checker exit 1"
assert_contains "$scratch_missing_out" "c_fixture_consumer" \
    "failure names the unlisted scratch consumer"
assert_contains "$scratch_missing_out" "absent from functionUsesImageAtomicScratch" \
    "failure explains which direction drifted"

# --- NEGATIVE CONTROL: a non-atomic declaration at the scratch slot ----------
# c_revoxelize_detached's real shape — slot 16 as a params UBO, deliberately
# NOT on the list (#1619). It must not be flagged, and the check must have
# actually seen it: the kernel count rises to 2 while the consumer count stays
# at 1. Without both halves this arm would also pass against a checker that
# skipped the file entirely.
SLOT_ALIAS="$TMPROOT/slot-alias"
make_fixture "$SLOT_ALIAS"
cat > "$SLOT_ALIAS/engine/render/src/shaders/metal/c_fixture_alias.metal" <<'EOF'
struct FixtureParams { int a; };
kernel void c_fixture_alias(
    constant FixtureParams& params [[buffer(16)]],
    uint3 gid [[thread_position_in_grid]]
) {}
EOF
write_pipeline_cpp "$SLOT_ALIAS" \
    "c_fixture_kernel c_fixture_alias" "c_fixture_kernel"
alias_out=$(run_checker "$SLOT_ALIAS")
alias_rc=$?
assert_eq "0" "$alias_rc" "non-atomic decl at the scratch slot is not flagged"
assert_contains "$alias_out" "Metal scratch-consumer check scanned 2 compute kernel(s)" \
    "the alias kernel was actually scanned, not skipped"
assert_contains "$alias_out" "(1 declare the image-atomic scratch at buffer slot 16)" \
    "the alias kernel is not counted as a scratch consumer"

# --- a commented-out list entry reads as absent, not present -----------------
# bindComputeResources binds off the live list, so commenting an entry out stops
# the bind exactly as deleting it would. A scan that harvests quoted names from
# the raw line reads the entry as present and passes green while the consumer's
# imageAtomicMin writes land nowhere — the forward direction, reached by two
# slashes.
SCRATCH_COMMENTED="$TMPROOT/scratch-commented"
make_fixture "$SCRATCH_COMMENTED"
write_pipeline_cpp "$SCRATCH_COMMENTED" "c_fixture_kernel" "" "c_fixture_kernel"
scratch_commented_out=$(run_checker "$SCRATCH_COMMENTED")
scratch_commented_rc=$?
assert_eq "1" "$scratch_commented_rc" "commented-out scratch list entry exits 1"
assert_contains "$scratch_commented_out" "c_fixture_kernel" \
    "failure names the consumer whose entry was commented out"
assert_contains "$scratch_commented_out" "absent from functionUsesImageAtomicScratch" \
    "a commented-out entry is reported as absent, not present"

# --- a hand-wrapped scratch declaration is still caught ----------------------
# The qualifier test reads the parameter's declaration window, not the physical
# line the slot attribute sits on. A line-scoped test reads this kernel as
# declaring no scratch and exits 0 — the same false clean as an omission, on a
# kernel that does declare it, reached by a line break alone. Only hand
# authoring produces the wrap: .metal is not in irreden_collect_quality_files'
# extension list, so no formatter can introduce or remove it.
SCRATCH_WRAPPED="$TMPROOT/scratch-wrapped"
make_fixture "$SCRATCH_WRAPPED"
cat > "$SCRATCH_WRAPPED/engine/render/src/shaders/metal/c_fixture_wrapped.metal" <<'EOF'
kernel void c_fixture_wrapped(
    device atomic_int*
        distanceScratch [[buffer(16)]],
    uint3 gid [[thread_position_in_grid]]
) {}
EOF
write_pipeline_cpp "$SCRATCH_WRAPPED" \
    "c_fixture_kernel c_fixture_wrapped" "c_fixture_kernel"
scratch_wrapped_out=$(run_checker "$SCRATCH_WRAPPED")
scratch_wrapped_rc=$?
assert_eq "1" "$scratch_wrapped_rc" "wrapped scratch declaration makes the checker exit 1"
assert_contains "$scratch_wrapped_out" "c_fixture_wrapped" \
    "failure names the kernel whose declaration wrapped"
assert_contains "$scratch_wrapped_out" "(2 declare the image-atomic scratch at buffer slot 16)" \
    "the wrapped declaration is counted, not read as absent"

# --- NEGATIVE CONTROL: an atomic neighbour sharing the slot line -------------
# The declaration window's other half. A line-scoped qualifier test sees the
# atomic_int belonging to the PRECEDING parameter and reads this kernel as a
# consumer — a false positive in the #1619 direction against a kernel whose
# slot-16 parameter is a plain params UBO. The counts pin that it was scanned
# and correctly excluded, not skipped.
SLOT_NEIGHBOUR="$TMPROOT/slot-neighbour"
make_fixture "$SLOT_NEIGHBOUR"
cat > "$SLOT_NEIGHBOUR/engine/render/src/shaders/metal/c_fixture_neighbour.metal" <<'EOF'
struct FixtureParams { int a; };
kernel void c_fixture_neighbour(
    device atomic_int* other [[buffer(3)]], constant FixtureParams& params [[buffer(16)]],
    uint3 gid [[thread_position_in_grid]]
) {}
EOF
write_pipeline_cpp "$SLOT_NEIGHBOUR" \
    "c_fixture_kernel c_fixture_neighbour" "c_fixture_kernel"
neighbour_out=$(run_checker "$SLOT_NEIGHBOUR")
neighbour_rc=$?
assert_eq "0" "$neighbour_rc" "an atomic neighbour on the slot line is not a consumer"
assert_contains "$neighbour_out" "Metal scratch-consumer check scanned 2 compute kernel(s)" \
    "the neighbour kernel was actually scanned, not skipped"
assert_contains "$neighbour_out" "(1 declare the image-atomic scratch at buffer slot 16)" \
    "an atomic at a different slot is not counted as a scratch declaration"

# --- reverse drift: a listed kernel that declares no scratch fails -----------
# The #1619 direction. A name on the list that does not declare the scratch
# gets the sticky scratch bound over whatever it does declare at that slot.
SCRATCH_STALE="$TMPROOT/scratch-stale"
make_fixture "$SCRATCH_STALE"
echo 'kernel void c_fixture_plain(uint3 gid [[thread_position_in_grid]]) {}' \
    > "$SCRATCH_STALE/engine/render/src/shaders/metal/c_fixture_plain.metal"
write_pipeline_cpp "$SCRATCH_STALE" \
    "c_fixture_kernel c_fixture_plain" "c_fixture_kernel c_fixture_plain"
scratch_stale_out=$(run_checker "$SCRATCH_STALE")
scratch_stale_rc=$?
assert_eq "1" "$scratch_stale_rc" "listed non-consumer makes the checker exit 1"
assert_contains "$scratch_stale_out" "c_fixture_plain" \
    "failure names the stale list entry"
assert_contains "$scratch_stale_out" "#1619" \
    "failure cites the regression the reverse direction reproduces"
assert_absent "$scratch_stale_out" "absent from functionUsesImageAtomicScratch" \
    "reverse drift is not reported as the forward direction"

# --- a scratch list without its bare "}" terminator fails --------------------
# Same EOF guard as the registry check's: a scan that ran past the terminator
# would sweep up unrelated quoted strings and could read a genuinely absent
# consumer as present.
SCRATCH_INDENTED="$TMPROOT/scratch-indented"
make_fixture "$SCRATCH_INDENTED"
cat > "$SCRATCH_INDENTED/engine/render/src/metal/metal_pipeline.cpp" <<'EOF'
namespace IRRender {
namespace {

MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
    if (functionName == "c_fixture_kernel") {
        return MTL::Size(16, 16, 1);
    }
    return MTL::Size(1, 1, 1);
}

}  // namespace

namespace detail {
    bool functionUsesImageAtomicScratch(const std::string &functionName) {
        return functionName == "c_fixture_kernel";
    }
}  // namespace detail
}  // namespace IRRender
EOF
scratch_indent_out=$(run_checker "$SCRATCH_INDENTED")
scratch_indent_rc=$?
assert_eq "1" "$scratch_indent_rc" "indented scratch list (no bare \"}\" line) exits 1"
assert_contains "$scratch_indent_out" "signature but hit EOF before its closing-brace terminator" \
    "scratch-list EOF-without-terminator failure explains itself"
# Both Metal checks carry that wording, so pin which one fired — the registry
# function is well-formed in this fixture and must have passed.
assert_contains "$scratch_indent_out" "run_metal_scratch_consumer_check.cmake" \
    "the scratch check is what rejected the indented list"
assert_contains "$scratch_indent_out" "Metal kernel registry check scanned" \
    "the registry check still passed on the same fixture"

# --- an unresolvable #include fails, rather than shrinking the scanned source -
# Dropping an include silently would make a real consumer read as "declares no
# scratch" — a false clean in the direction this check exists to close.
BAD_INCLUDE="$TMPROOT/bad-include"
make_fixture "$BAD_INCLUDE"
cat > "$BAD_INCLUDE/engine/render/src/shaders/metal/c_fixture_kernel.metal" <<'EOF'
// fixture kernel
#include "c_fixture_kernel_missing_body.metal"
EOF
bad_include_out=$(run_checker "$BAD_INCLUDE")
bad_include_rc=$?
assert_eq "1" "$bad_include_rc" "unresolvable #include exits 1"
assert_contains "$bad_include_out" "could not resolve #include" \
    "unresolvable-include failure explains itself"
assert_contains "$bad_include_out" "c_fixture_kernel_missing_body.metal" \
    "failure names the include it could not resolve"

# --- a tree where nothing matches the slot attribute fails -------------------
# The slot is aliased and always has consumers, so zero matches means the
# attribute spelling drifted, not that the slot went unused. Without this the
# forward direction would read clean on an expected set the matcher can no
# longer populate.
NO_SLOT="$TMPROOT/no-slot"
make_fixture "$NO_SLOT"
cat > "$NO_SLOT/engine/render/src/shaders/metal/c_fixture_kernel_body.metal" <<'EOF'
// fixture include-fragment with no slot-attributed parameter
kernel void IR_FIXTURE_KERNEL_NAME(uint3 gid [[thread_position_in_grid]]) {}
EOF
no_slot_out=$(run_checker "$NO_SLOT")
no_slot_rc=$?
assert_eq "1" "$no_slot_rc" "a tree with no slot-attributed declaration exits 1"
assert_contains "$no_slot_out" "found no [[buffer(16)]] declaration anywhere" \
    "empty-matcher failure explains itself rather than reporting a clean scan"
assert_contains "$no_slot_out" "spelling changed" \
    "empty-matcher failure names the likely cause"

# --- an unreadable scratch-slot constant fails ------------------------------
# The check parses the slot number out of kMetalImageAtomicScratchSlot rather
# than hardcoding it; a rename must be loud, not silently scanned against a
# stale literal.
SLOT_RENAMED="$TMPROOT/slot-renamed"
make_fixture "$SLOT_RENAMED"
cat > "$SLOT_RENAMED/engine/render/include/irreden/render/metal/metal_runtime.hpp" <<'EOF'
#pragma once
namespace IRRender {
constexpr std::uint32_t kMetalScratchSlotRenamed = 16;
}
EOF
slot_out=$(run_checker "$SLOT_RENAMED")
slot_rc=$?
assert_eq "1" "$slot_rc" "unreadable scratch-slot constant exits 1"
assert_contains "$slot_out" "could not read kMetalImageAtomicScratchSlot" \
    "slot-constant anchor failure explains itself"

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

# --- the same global under a render-backend path fails (#2889) --------------
# The style tools reject engine/render/**/gl_wrap/ and
# engine/render/**/metal/ — clang-format rewrites the generated GL wrapper and
# clang-tidy trips on metal-cpp idioms. That is a STYLE exemption, so the
# correctness gate must not inherit it: irreden_collect_quality_files takes
# INCLUDE_RENDER_BACKENDS to keep those 9 first-party headers in scope (#2815).
#
# This entry point is the only one CI runs, so the file set it collects IS the
# merge gate. The arms above cannot pin that: they put their violation under
# engine/include/, which the narrow and wide scopes both already reach. Hence a
# fixture under each rejected path — these arms go red if the shim ever drops
# INCLUDE_RENDER_BACKENDS (#2889).
BACKEND="$TMPROOT/backend"
make_fixture "$BACKEND"
mkdir -p "$BACKEND/engine/render/include/irreden/render/metal" \
         "$BACKEND/engine/render/include/irreden/render/gl_wrap"
cat > "$BACKEND/engine/render/include/irreden/render/metal/metal_probe.hpp" <<'EOF'
#pragma once
namespace IRFixture {
inline int g_metalBackendGlobal = 0;
}
EOF
cat > "$BACKEND/engine/render/include/irreden/render/gl_wrap/gl_probe.h" <<'EOF'
#pragma once
namespace IRFixture {
inline int g_glWrapBackendGlobal = 0;
}
EOF
backend_out=$(run_checker "$BACKEND")
backend_rc=$?
assert_eq "1" "$backend_rc" "header global under a render-backend path exits 1"
assert_contains "$backend_out" "metal_probe.hpp" "failure names the metal header"
assert_contains "$backend_out" "g_metalBackendGlobal" "failure names the metal declaration"
assert_contains "$backend_out" "gl_probe.h" "failure names the gl_wrap header"
assert_contains "$backend_out" "g_glWrapBackendGlobal" "failure names the gl_wrap declaration"

# --- a void-pointer global fails (#2916 Defect 2: the old undocumented
# `void` reject exempted this mutable, unowned pointer) ---------------------
VOIDPTR="$TMPROOT/voidptr"
make_fixture "$VOIDPTR"
cat > "$VOIDPTR/engine/include/irreden/voidptr.hpp" <<'EOF'
#pragma once
namespace IRFixture {
inline void *g_metalDevice = nullptr;
}
EOF
voidptr_out=$(run_checker "$VOIDPTR")
voidptr_rc=$?
assert_eq "1" "$voidptr_rc" "void-pointer header global makes the checker exit 1"
assert_contains "$voidptr_out" "g_metalDevice" \
    "failure names the void-pointer declaration"

# --- a void function still passes (the deleted `void` reject was redundant
# for this case — the function-declaration guard already catches it) -------
VOIDFN="$TMPROOT/voidfn"
make_fixture "$VOIDFN"
cat > "$VOIDFN/engine/include/irreden/voidfn.hpp" <<'EOF'
#pragma once
namespace IRFixture {
inline void doThing() {}
}
EOF
voidfn_out=$(run_checker "$VOIDFN")
voidfn_rc=$?
assert_eq "0" "$voidfn_rc" "void function still passes without the void reject"

# --- a declaration whose terminator wraps onto a continuation line fails
# (#2916 Defect 1: the formatter-defeat case — the repo's own 100-col
# clang-format wraps a long `inline` declaration exactly like this) ---------
WRAPPED="$TMPROOT/wrapped"
make_fixture "$WRAPPED"
cat > "$WRAPPED/engine/include/irreden/wrapped.hpp" <<'EOF'
#pragma once
#include <unordered_map>
namespace IRFixture {
inline std::unordered_map<int, int>
    g_wrappedRegistry;
}
EOF
wrapped_out=$(run_checker "$WRAPPED")
wrapped_rc=$?
assert_eq "1" "$wrapped_rc" "wrapped-declaration header global makes the checker exit 1"
assert_contains "$wrapped_out" "g_wrappedRegistry" \
    "failure names the wrapped declaration"

# --- a both-ends-const pointer is a program constant and stays exempt -------
REALCONST="$TMPROOT/realconst"
make_fixture "$REALCONST"
cat > "$REALCONST/engine/include/irreden/realconst.hpp" <<'EOF'
#pragma once
namespace IRFixture {
inline const char *const g_realConstName = "ok";
}
EOF
realconst_out=$(run_checker "$REALCONST")
realconst_rc=$?
assert_eq "0" "$realconst_rc" "both-ends-const pointer stays exempt"

# A header the scan never reached exits 0 too, so the count is what makes the
# exemption a measurement, not just an exit code: the clean fixture scans 2
# (clean.hpp plus the render-backend metal_runtime.hpp, in scope since #2889
# widened the collector to INCLUDE_RENDER_BACKENDS), so this fixture must
# scan 3. Both numbers are measured against make_fixture — re-measure them if
# it grows a header, rather than assuming the delta.
assert_contains "$realconst_out" "scanned 3 header file(s)" \
    "the exempt header entered the scan rather than skipping the candidate gate"

# --- single-sided const on a pointer is still a banned global ---------------
# Both halves live in one header so the two failures prove the scan read this
# file — which is what makes the third assertion (the both-ends form is NOT
# named) evidence about the reject chain rather than about a skipped file.
# This is the shape that hid a real `g_activeShots` violation through an entire
# hand-grep pass (#2726), and .claude/rules/cpp-globals.md calls it out by name.
HALFCONST="$TMPROOT/halfconst"
make_fixture "$HALFCONST"
cat > "$HALFCONST/engine/include/irreden/halfconst.hpp" <<'EOF'
#pragma once
namespace IRFixture {
inline const char *const g_bothEndsConst = "ok";
inline const char *g_pointeeConstOnly = "reseatable";
inline char *const g_handleConstOnly = nullptr;
}
EOF
halfconst_out=$(run_checker "$HALFCONST")
halfconst_rc=$?
assert_eq "1" "$halfconst_rc" "single-sided-const pointer globals exit 1"
assert_contains "$halfconst_out" "g_pointeeConstOnly" \
    "leading const alone leaves a reseatable pointer, still flagged"
assert_contains "$halfconst_out" "g_handleConstOnly" \
    "trailing const alone freezes the handle, not the data, still flagged"
assert_absent "$halfconst_out" "g_bothEndsConst" \
    "both-ends-const in that same scanned header is exempt"

# --- usage guard ------------------------------------------------------------
noroot_out=$(cmake -P "$CHECKER" 2>&1)
noroot_rc=$?
assert_eq "1" "$noroot_rc" "missing PROJECT_ROOT exits 1"
assert_contains "$noroot_out" "PROJECT_ROOT is required" \
    "missing PROJECT_ROOT explains itself"

summarize "run_header_checks_standalone tests"

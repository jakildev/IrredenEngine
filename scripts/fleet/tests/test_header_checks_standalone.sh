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
#   - a component missing its save decision         → exit 1, names its header
#   - a decided component missing from the tuple    → exit 1, names the type
#   - a missing save-inventory anchor               → exit 1 (anchor guard)
#   - an unregistered Metal compute kernel          → exit 1, names the kernel
#   - a line- or block-commented registry entry     → exit 1, names the kernel
#     (a commented-out entry never reaches the compiled binary, so it must
#     read as absent, not present — both comment forms, #2899)
#   - a MULTI-LINE block-commented registry entry    → exit 1 (the registry's
#     real entries are multi-line `if` conditions, so disabling one wraps
#     several lines, not one — a same-line-only strip false-cleans this shape)
#   - a registry entry inside a preprocessor #if    → exit 1, names the kernel
#     (#2983 — a source-text scan can't evaluate which branch the build
#     takes, so it must read as absent, not present, same as a comment)
#   - a registry entry inside #ifdef/#ifndef        → exit 1, same reasoning
#     (pins the #ifdef/#ifndef spellings the doc comments claim are
#     symmetric with #if — #if 0 was the only spelling covered until now)
#   - a registry with no bare "}" terminator line   → exit 1 (EOF guard, not
#     a silent scan past the function into unrelated string literals)
#   - a scratch consumer absent from the list       → exit 1, names the kernel
#   - a non-atomic decl at the scratch slot         → exit 0 (the #1619 params
#     UBO shape must NOT be flagged — negative control)
#   - a line- or block-commented list entry         → exit 1 (the runtime stops
#     binding for it, so it must read as absent, not present — both comment
#     forms, #2899)
#   - a MULTI-LINE block-commented list entry        → exit 1 (disabling a run
#     of consecutive entries is the natural reason to reach for a block
#     comment here, and that spans lines)
#   - a list entry inside a preprocessor #if        → exit 1, names the kernel
#     (#2983, same reasoning as the registry-side arm)
#   - a #if literal inside a comment, followed by   → exit 0, entry recognized
#     a real uncommented list entry                   (#2983 follow-up: the
#     conditional check must run AFTER the comment-strip, or a commented-out
#     #if starves conditional_depth and every entry after it reads as absent)
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
#   - a wrapped declaration whose head line carries a trailing comment with
#     a paren in it                                 → exit 1 (the comment's
#     `(` must not read as a function-declaration guard hit)
#
# Plus one section that asserts what the shim RUNS rather than what it finds
# (#3118): every cmake/run_*check*.cmake other than the shim itself must be
# directly include()d by it, since the shim is the only path CI executes. The
# population comes from the glob, never from the shim's includes or
# make_fixture's copy list (#2876), and header-checks.yml's paths: filters must
# match a checker filename that does not exist yet, so a new checker triggers
# the job that runs the census.

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
# $4 (optional) is a list of scratch entries emitted LINE-COMMENTED OUT (//) —
# the way a developer disables one, rather than deleting it. bindComputeResources
# binds off the live list, so those names must read as absent. $5 is the same
# but BLOCK-COMMENTED (/* ... */) — the sibling comment form #2899 closes. $6
# and $7 are the registry-side twins: kernel names whose
# threadgroupSizeForFunctionName entry is emitted commented out, line-style and
# block-style respectively.
write_pipeline_cpp() {
    local root="$1" registry_names="$2" scratch_names="$3" commented_names="${4:-}" \
          commented_names_block="${5:-}" registry_commented_names="${6:-}" \
          registry_commented_names_block="${7:-}" name
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
        for name in $registry_commented_names; do
            echo "    // if (functionName == \"$name\") { return MTL::Size(16, 16, 1); }"
        done
        for name in $registry_commented_names_block; do
            echo "    /* if (functionName == \"$name\") { return MTL::Size(16, 16, 1); } */"
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
        for name in $commented_names_block; do
            echo "        /* || functionName == \"$name\" */"
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
             "$root/engine/world/include/irreden/world" \
             "$root/engine/render/src/shaders/metal" \
             "$root/engine/render/src/metal" \
             "$root/engine/render/include/irreden/render/metal"
    cp "$SCRIPT_DIR/cmake/ir_quality_tools.cmake" \
       "$SCRIPT_DIR/cmake/run_header_convention_checks.cmake" \
       "$SCRIPT_DIR/cmake/run_metal_kernel_registry_check.cmake" \
       "$SCRIPT_DIR/cmake/run_metal_scratch_consumer_check.cmake" \
       "$SCRIPT_DIR/cmake/run_save_inventory_population_check.cmake" \
       "$CHECKER" "$root/cmake/"
    cat > "$root/engine/include/irreden/clean.hpp" <<'EOF'
#pragma once
namespace IRFixture {
constexpr int kCleanConstant = 1;
const char *const kCleanName = "clean";
}
EOF
    cat > "$root/engine/include/irreden/components_fixture.hpp" <<'EOF'
#pragma once
namespace IRComponents {
struct C_FixtureSaved {};
struct C_FixtureSkipped {};
// struct C_LineCommented {};
/* struct C_BlockCommented {}; */
}
EOF
    cat > "$root/engine/world/include/irreden/world/save_component_inventory.hpp" <<'EOF'
#pragma once
IR_SAVE_OPT_IN(IRComponents::C_FixtureSaved, 1)
IR_SAVE_OPT_OUT(IRComponents::C_FixtureSkipped)
using AllEngineComponents = std::tuple<
    IRComponents::C_FixtureSaved,
    IRComponents::C_FixtureSkipped>;
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
assert_contains "$clean_out" "Save inventory population check scanned 2 component name(s)" \
    "save inventory check scans live declarations and ignores comments"

# --- an undeclared component needs an explicit save decision ----------------
SAVE_MISSING_DECISION="$TMPROOT/save-missing-decision"
make_fixture "$SAVE_MISSING_DECISION"
echo 'struct C_FixtureOrphan {};' \
    >> "$SAVE_MISSING_DECISION/engine/include/irreden/components_fixture.hpp"
save_missing_decision_out=$(run_checker "$SAVE_MISSING_DECISION")
save_missing_decision_rc=$?
assert_eq "1" "$save_missing_decision_rc" \
    "component missing its save decision makes the checker exit 1"
assert_contains "$save_missing_decision_out" "C_FixtureOrphan" \
    "missing-decision failure names the component"
assert_contains "$save_missing_decision_out" "components_fixture.hpp" \
    "missing-decision failure names the declaring header"
assert_contains "$save_missing_decision_out" "missing decision" \
    "missing-decision failure explains the inventory omission"

# --- a decision without a tuple entry stays incomplete ----------------------
SAVE_MISSING_TUPLE="$TMPROOT/save-missing-tuple"
make_fixture "$SAVE_MISSING_TUPLE"
echo 'struct C_FixtureOrphan {};' \
    >> "$SAVE_MISSING_TUPLE/engine/include/irreden/components_fixture.hpp"
echo 'IR_SAVE_OPT_IN(IRComponents::C_FixtureOrphan, 1)' \
    >> "$SAVE_MISSING_TUPLE/engine/world/include/irreden/world/save_component_inventory.hpp"
save_missing_tuple_out=$(run_checker "$SAVE_MISSING_TUPLE")
save_missing_tuple_rc=$?
assert_eq "1" "$save_missing_tuple_rc" \
    "decided component missing its tuple entry makes the checker exit 1"
assert_contains "$save_missing_tuple_out" "C_FixtureOrphan" \
    "missing-tuple failure names the component"
assert_contains "$save_missing_tuple_out" "missing tuple entry" \
    "missing-tuple failure explains the incomplete inventory"
assert_absent "$save_missing_tuple_out" "missing decision" \
    "the decision-only fixture reaches the tuple-specific failure"

# --- a missing inventory anchor fails closed --------------------------------
SAVE_MISSING_ANCHOR="$TMPROOT/save-missing-anchor"
make_fixture "$SAVE_MISSING_ANCHOR"
rm "$SAVE_MISSING_ANCHOR/engine/world/include/irreden/world/save_component_inventory.hpp"
save_missing_anchor_out=$(run_checker "$SAVE_MISSING_ANCHOR")
save_missing_anchor_rc=$?
assert_eq "1" "$save_missing_anchor_rc" "missing save inventory exits 1"
assert_contains "$save_missing_anchor_out" "not found" \
    "missing save inventory anchor explains itself"

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

# --- a line-commented (//) registry entry reads as absent, not present -------
# A kernel absent from threadgroupSizeForFunctionName silently falls through to
# the MTL::Size(1, 1, 1) fallback -- no assert, no log, no build error -- so a
# commented-out entry must fail exactly like an omitted one.
REGISTRY_COMMENTED="$TMPROOT/registry-commented"
make_fixture "$REGISTRY_COMMENTED"
echo '// registry-commented fixture kernel' \
    > "$REGISTRY_COMMENTED/engine/render/src/shaders/metal/c_registry_commented.metal"
write_pipeline_cpp "$REGISTRY_COMMENTED" "c_fixture_kernel" "c_fixture_kernel" "" "" \
    "c_registry_commented"
registry_commented_out=$(run_checker "$REGISTRY_COMMENTED")
registry_commented_rc=$?
assert_eq "1" "$registry_commented_rc" "line-commented-out registry entry exits 1"
assert_contains "$registry_commented_out" "c_registry_commented" \
    "failure names the kernel whose registry entry was commented out"
assert_contains "$registry_commented_out" "no entry in" \
    "a commented-out registry entry is reported as absent, not present"

# --- a block-commented (/* */) registry entry reads as absent, not present ---
REGISTRY_COMMENTED_BLOCK="$TMPROOT/registry-commented-block"
make_fixture "$REGISTRY_COMMENTED_BLOCK"
echo '// registry-commented-block fixture kernel' \
    > "$REGISTRY_COMMENTED_BLOCK/engine/render/src/shaders/metal/c_registry_commented_block.metal"
write_pipeline_cpp "$REGISTRY_COMMENTED_BLOCK" "c_fixture_kernel" "c_fixture_kernel" "" "" "" \
    "c_registry_commented_block"
registry_commented_block_out=$(run_checker "$REGISTRY_COMMENTED_BLOCK")
registry_commented_block_rc=$?
assert_eq "1" "$registry_commented_block_rc" "block-commented-out registry entry exits 1"
assert_contains "$registry_commented_block_out" "c_registry_commented_block" \
    "failure names the kernel whose registry entry was block-commented out"
assert_contains "$registry_commented_block_out" "no entry in" \
    "a block-commented registry entry is reported as absent, not present"

# --- a MULTI-LINE block-commented registry entry reads as absent -------------
# The real registry's entries are multi-line `if` conditions
# (metal_pipeline.cpp), so disabling one wraps the block comment across
# several lines -- the shape write_pipeline_cpp's per-name single-line echo
# can't express, hence the direct fixture write. A same-line-only strip reads
# every interior line as still live (Opus recheck on #2959).
REGISTRY_COMMENTED_BLOCK_MULTILINE="$TMPROOT/registry-commented-block-multiline"
make_fixture "$REGISTRY_COMMENTED_BLOCK_MULTILINE"
echo '// registry-commented-block-multiline fixture kernel' \
    > "$REGISTRY_COMMENTED_BLOCK_MULTILINE/engine/render/src/shaders/metal/c_registry_commented_multiline.metal"
cat > "$REGISTRY_COMMENTED_BLOCK_MULTILINE/engine/render/src/metal/metal_pipeline.cpp" <<'EOF'
namespace IRRender {
namespace {

MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
    if (functionName == "c_fixture_kernel") {
        return MTL::Size(16, 16, 1);
    }
    /* if (functionName == "c_registry_commented_multiline") {
        return MTL::Size(16, 16, 1);
    } */
    return MTL::Size(1, 1, 1);
}

bool functionUsesImageAtomicScratch(const std::string &functionName) {
    return false
           || functionName == "c_fixture_kernel"
        ;
}

}  // namespace
}  // namespace IRRender
EOF
registry_commented_block_multiline_out=$(run_checker "$REGISTRY_COMMENTED_BLOCK_MULTILINE")
registry_commented_block_multiline_rc=$?
assert_eq "1" "$registry_commented_block_multiline_rc" \
    "multi-line block-commented-out registry entry exits 1"
assert_contains "$registry_commented_block_multiline_out" "c_registry_commented_multiline" \
    "failure names the kernel whose registry entry was multi-line block-commented out"
assert_contains "$registry_commented_block_multiline_out" "no entry in" \
    "a multi-line block-commented registry entry is reported as absent, not present"

# --- a registry entry inside a preprocessor conditional reads as absent -----
# threadgroupSizeForFunctionName is scanned as source text with no
# preprocessor, so it cannot evaluate which #if/#ifdef/#ifndef branch the
# build takes -- an entry that exists only inside one may never reach the
# compiled binary. It must fail exactly like an omitted entry, the same
# "read as absent" contract #2899 established for comments (#2983). Direct
# fixture write, not write_pipeline_cpp, since that helper has no conditional
# shape.
REGISTRY_CONDITIONAL="$TMPROOT/registry-conditional"
make_fixture "$REGISTRY_CONDITIONAL"
echo '// registry-conditional fixture kernel' \
    > "$REGISTRY_CONDITIONAL/engine/render/src/shaders/metal/c_registry_conditional.metal"
cat > "$REGISTRY_CONDITIONAL/engine/render/src/metal/metal_pipeline.cpp" <<'EOF'
namespace IRRender {
namespace {

MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
    if (functionName == "c_fixture_kernel") {
        return MTL::Size(16, 16, 1);
    }
#if 0
    if (functionName == "c_registry_conditional") {
        return MTL::Size(16, 16, 1);
    }
#endif
    return MTL::Size(1, 1, 1);
}

bool functionUsesImageAtomicScratch(const std::string &functionName) {
    return false
           || functionName == "c_fixture_kernel"
        ;
}

}  // namespace
}  // namespace IRRender
EOF
registry_conditional_out=$(run_checker "$REGISTRY_CONDITIONAL")
registry_conditional_rc=$?
assert_eq "1" "$registry_conditional_rc" \
    "registry entry inside a preprocessor conditional exits 1"
assert_contains "$registry_conditional_out" "c_registry_conditional" \
    "failure names the kernel whose registry entry is inside a conditional"
assert_contains "$registry_conditional_out" "no entry in" \
    "a conditional registry entry is reported as absent, not present"

# --- the same reads as absent under #ifdef/#ifndef, not just #if -----------
# Both .cmake checkers' doc comments claim symmetry across all three
# preprocessor-conditional spellings (#if/#ifdef/#ifndef), but until now the
# suite only ever exercised #if 0 (nit carried across three reviews on
# #2984). Pin the #ifdef spelling on the registry checker; the underlying
# regex is `^[ \t]*#[ \t]*(if|ifdef|ifndef)([ \t(]|$)`, shared verbatim by
# the scratch-consumer checker, so one arm covers both.
REGISTRY_CONDITIONAL_IFDEF="$TMPROOT/registry-conditional-ifdef"
make_fixture "$REGISTRY_CONDITIONAL_IFDEF"
echo '// registry-conditional-ifdef fixture kernel' \
    > "$REGISTRY_CONDITIONAL_IFDEF/engine/render/src/shaders/metal/c_registry_conditional_ifdef.metal"
cat > "$REGISTRY_CONDITIONAL_IFDEF/engine/render/src/metal/metal_pipeline.cpp" <<'EOF'
namespace IRRender {
namespace {

MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
    if (functionName == "c_fixture_kernel") {
        return MTL::Size(16, 16, 1);
    }
#ifdef IR_OLD_METAL_PATH
    if (functionName == "c_registry_conditional_ifdef") {
        return MTL::Size(16, 16, 1);
    }
#endif
    return MTL::Size(1, 1, 1);
}

bool functionUsesImageAtomicScratch(const std::string &functionName) {
    return false
           || functionName == "c_fixture_kernel"
        ;
}

}  // namespace
}  // namespace IRRender
EOF
registry_conditional_ifdef_out=$(run_checker "$REGISTRY_CONDITIONAL_IFDEF")
registry_conditional_ifdef_rc=$?
assert_eq "1" "$registry_conditional_ifdef_rc" \
    "registry entry inside an #ifdef conditional exits 1"
assert_contains "$registry_conditional_ifdef_out" "c_registry_conditional_ifdef" \
    "failure names the kernel whose registry entry is inside an #ifdef"
assert_contains "$registry_conditional_ifdef_out" "no entry in" \
    "an #ifdef-guarded registry entry is reported as absent, not present"


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

# --- a block-commented (/* */) list entry reads as absent, not present -------
# The line-comment form above is fixed by #2886; this is the other comment
# shape a developer reaches for to disable an entry (#2899).
SCRATCH_COMMENTED_BLOCK="$TMPROOT/scratch-commented-block"
make_fixture "$SCRATCH_COMMENTED_BLOCK"
write_pipeline_cpp "$SCRATCH_COMMENTED_BLOCK" "c_fixture_kernel" "" "" "c_fixture_kernel"
scratch_commented_block_out=$(run_checker "$SCRATCH_COMMENTED_BLOCK")
scratch_commented_block_rc=$?
assert_eq "1" "$scratch_commented_block_rc" "block-commented-out scratch list entry exits 1"
assert_contains "$scratch_commented_block_out" "c_fixture_kernel" \
    "failure names the consumer whose entry was block-commented out"
assert_contains "$scratch_commented_block_out" "absent from functionUsesImageAtomicScratch" \
    "a block-commented entry is reported as absent, not present"

# --- a MULTI-LINE block-commented list entry reads as absent -----------------
# Disabling a run of consecutive entries at once is the natural reason to
# reach for a block comment in this list, and that spans lines -- the shape
# write_pipeline_cpp's per-name single-line echo can't express, hence the
# direct fixture write (Opus recheck on #2959).
SCRATCH_COMMENTED_BLOCK_MULTILINE="$TMPROOT/scratch-commented-block-multiline"
make_fixture "$SCRATCH_COMMENTED_BLOCK_MULTILINE"
cat > "$SCRATCH_COMMENTED_BLOCK_MULTILINE/engine/render/src/shaders/metal/c_scratch_commented_multiline.metal" <<'EOF'
kernel void c_scratch_commented_multiline(
    device atomic_int* distanceScratch [[buffer(16)]],
    uint3 gid [[thread_position_in_grid]]
) {}
EOF
cat > "$SCRATCH_COMMENTED_BLOCK_MULTILINE/engine/render/src/metal/metal_pipeline.cpp" <<'EOF'
namespace IRRender {
namespace {

MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
    if (functionName == "c_fixture_kernel") {
        return MTL::Size(16, 16, 1);
    }
    if (functionName == "c_scratch_commented_multiline") {
        return MTL::Size(16, 16, 1);
    }
    return MTL::Size(1, 1, 1);
}

bool functionUsesImageAtomicScratch(const std::string &functionName) {
    return false
           || functionName == "c_fixture_kernel"
        /*
        || functionName == "c_scratch_commented_multiline"
        */
        ;
}

}  // namespace
}  // namespace IRRender
EOF
scratch_commented_block_multiline_out=$(run_checker "$SCRATCH_COMMENTED_BLOCK_MULTILINE")
scratch_commented_block_multiline_rc=$?
assert_eq "1" "$scratch_commented_block_multiline_rc" \
    "multi-line block-commented-out scratch list entry exits 1"
assert_contains "$scratch_commented_block_multiline_out" "c_scratch_commented_multiline" \
    "failure names the consumer whose entry was multi-line block-commented out"
assert_contains "$scratch_commented_block_multiline_out" "absent from functionUsesImageAtomicScratch" \
    "a multi-line block-commented entry is reported as absent, not present"

# --- a scratch list entry inside a preprocessor conditional reads as absent --
# Same reasoning as the registry-side arm above (#2983): functionUsesImage-
# AtomicScratch is scanned as source text with no preprocessor, so an entry
# that exists only inside #if/#ifdef/#ifndef must fail exactly like an
# omitted entry. The kernel itself declares the scratch (so it would
# otherwise be a genuine expected consumer) and is registered unconditionally
# in threadgroupSizeForFunctionName -- only its list entry is conditional.
# Direct fixture write, not write_pipeline_cpp, since that helper has no
# conditional shape.
SCRATCH_CONDITIONAL="$TMPROOT/scratch-conditional"
make_fixture "$SCRATCH_CONDITIONAL"
cat > "$SCRATCH_CONDITIONAL/engine/render/src/shaders/metal/c_scratch_conditional.metal" <<'EOF'
kernel void c_scratch_conditional(
    device atomic_int* distanceScratch [[buffer(16)]],
    uint3 gid [[thread_position_in_grid]]
) {}
EOF
cat > "$SCRATCH_CONDITIONAL/engine/render/src/metal/metal_pipeline.cpp" <<'EOF'
namespace IRRender {
namespace {

MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
    if (functionName == "c_fixture_kernel") {
        return MTL::Size(16, 16, 1);
    }
    if (functionName == "c_scratch_conditional") {
        return MTL::Size(16, 16, 1);
    }
    return MTL::Size(1, 1, 1);
}

bool functionUsesImageAtomicScratch(const std::string &functionName) {
    return false
           || functionName == "c_fixture_kernel"
#if 0
           || functionName == "c_scratch_conditional"
#endif
        ;
}

}  // namespace
}  // namespace IRRender
EOF
scratch_conditional_out=$(run_checker "$SCRATCH_CONDITIONAL")
scratch_conditional_rc=$?
assert_eq "1" "$scratch_conditional_rc" \
    "scratch list entry inside a preprocessor conditional exits 1"
assert_contains "$scratch_conditional_out" "c_scratch_conditional" \
    "failure names the consumer whose list entry is inside a conditional"
assert_contains "$scratch_conditional_out" "absent from functionUsesImageAtomicScratch" \
    "a conditional list entry is reported as absent, not present"

# --- a #if literal inside a comment must not starve a later real entry -------
# The bug this arm pins (#2983 follow-up): the conditional check originally
# ran BEFORE the comment-strip, so a #if-shaped line sitting inside a /* */
# comment was misread as a live preprocessor directive. Since the comment's
# own "*/" line never reaches the #endif branch either (it gets skipped by
# the still-elevated conditional_depth before the comment-strip can close
# in_block_comment), conditional_depth got stuck above zero for the rest of
# the function body and every subsequent real, uncommented, non-conditional
# entry was silently dropped. c_scratch_after_comment_conditional is a
# genuine consumer (declares the atomic scratch, registered unconditionally)
# whose list entry sits right after such a comment -- it must be recognized,
# not read as absent.
SCRATCH_AFTER_COMMENT_CONDITIONAL="$TMPROOT/scratch-after-comment-conditional"
make_fixture "$SCRATCH_AFTER_COMMENT_CONDITIONAL"
cat > "$SCRATCH_AFTER_COMMENT_CONDITIONAL/engine/render/src/shaders/metal/c_scratch_after_comment_conditional.metal" <<'EOF'
kernel void c_scratch_after_comment_conditional(
    device atomic_int* distanceScratch [[buffer(16)]],
    uint3 gid [[thread_position_in_grid]]
) {}
EOF
cat > "$SCRATCH_AFTER_COMMENT_CONDITIONAL/engine/render/src/metal/metal_pipeline.cpp" <<'EOF'
namespace IRRender {
namespace {

MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
    if (functionName == "c_fixture_kernel") {
        return MTL::Size(16, 16, 1);
    }
    if (functionName == "c_scratch_after_comment_conditional") {
        return MTL::Size(16, 16, 1);
    }
    return MTL::Size(1, 1, 1);
}

bool functionUsesImageAtomicScratch(const std::string &functionName) {
    return false
           || functionName == "c_fixture_kernel"
        /*
        #if OLD_APPROACH_NOTE
        */
        || functionName == "c_scratch_after_comment_conditional"
        ;
}

}  // namespace
}  // namespace IRRender
EOF
scratch_after_comment_conditional_out=$(run_checker "$SCRATCH_AFTER_COMMENT_CONDITIONAL")
scratch_after_comment_conditional_rc=$?
assert_eq "0" "$scratch_after_comment_conditional_rc" \
    "a #if literal inside a comment does not starve a later real list entry"
assert_contains "$scratch_after_comment_conditional_out" \
    "Metal scratch-consumer check scanned 2 compute kernel(s)" \
    "the post-comment kernel was actually scanned, not skipped"
assert_absent "$scratch_after_comment_conditional_out" \
    "absent from functionUsesImageAtomicScratch" \
    "the entry after the comment is recognized, not read as absent"

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

# --- block-commented globals are dead, while live controls still flag -------
GLOBAL_COMMENTED_SAME_LINE="$TMPROOT/global-commented-same-line"
make_fixture "$GLOBAL_COMMENTED_SAME_LINE"
cat > "$GLOBAL_COMMENTED_SAME_LINE/engine/include/irreden/global_comment_same_line.hpp" <<'EOF'
#pragma once
namespace IRFixture {
/* inline int g_sameLineCommented = 0; */ inline int g_sameLineLive = 0;
}
EOF
global_same_line_out=$(run_checker "$GLOBAL_COMMENTED_SAME_LINE")
global_same_line_rc=$?
assert_eq "1" "$global_same_line_rc" \
    "same-line block-comment fixture retains its live global control"
assert_contains "$global_same_line_out" "g_sameLineLive" \
    "same-line fixture flags the live declaration"
assert_absent "$global_same_line_out" "g_sameLineCommented" \
    "same-line block-commented global is ignored"

GLOBAL_COMMENTED_MULTILINE="$TMPROOT/global-commented-multiline"
make_fixture "$GLOBAL_COMMENTED_MULTILINE"
cat > "$GLOBAL_COMMENTED_MULTILINE/engine/include/irreden/global_comment_multiline.hpp" <<'EOF'
#pragma once
namespace IRFixture {
/*
inline int g_multilineCommented = 0;
*/ inline int g_multilineLive = 0;
}
EOF
global_multiline_out=$(run_checker "$GLOBAL_COMMENTED_MULTILINE")
global_multiline_rc=$?
assert_eq "1" "$global_multiline_rc" \
    "multi-line block-comment fixture retains its live global control"
assert_contains "$global_multiline_out" "g_multilineLive" \
    "multi-line fixture flags the live declaration"
assert_absent "$global_multiline_out" "g_multilineCommented" \
    "multi-line block-commented global is ignored"

# --- block-commented anonymous namespaces are dead, while live still flags --
ANONYMOUS_COMMENTED="$TMPROOT/anonymous-commented"
make_fixture "$ANONYMOUS_COMMENTED"
cat > "$ANONYMOUS_COMMENTED/engine/include/irreden/anonymous_live.hpp" <<'EOF'
#pragma once
namespace {
constexpr int kLiveAnonymous = 1;
}
EOF
cat > "$ANONYMOUS_COMMENTED/engine/include/irreden/anonymous_same_line.hpp" <<'EOF'
#pragma once
/* namespace { constexpr int kCommentedAnonymous = 1; } */
EOF
cat > "$ANONYMOUS_COMMENTED/engine/include/irreden/anonymous_multiline.hpp" <<'EOF'
#pragma once
/*
namespace {
constexpr int kCommentedAnonymous = 1;
}
*/
EOF
anonymous_commented_out=$(run_checker "$ANONYMOUS_COMMENTED")
anonymous_commented_rc=$?
assert_eq "1" "$anonymous_commented_rc" \
    "anonymous-namespace comment fixture retains its live control"
assert_contains "$anonymous_commented_out" "anonymous_live.hpp" \
    "live anonymous namespace is still flagged"
assert_absent "$anonymous_commented_out" "anonymous_same_line.hpp" \
    "same-line block-commented anonymous namespace is ignored"
assert_absent "$anonymous_commented_out" "anonymous_multiline.hpp" \
    "multi-line block-commented anonymous namespace is ignored"

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
# exemption a measurement, not just an exit code: the clean fixture scans 4
# (the two general headers, the inventory anchor, and the render-backend
# metal_runtime.hpp), so this fixture must scan 5. Both numbers are measured
# against make_fixture — re-measure them if it grows a header, rather than
# assuming the delta.
assert_contains "$realconst_out" "scanned 5 header file(s)" \
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

# --- a paren inside a trailing comment on a wrapped head line still flags ---
# The join buffer used to carry the raw head line verbatim, so a `(` inside
# `// registry (id -> slot)` on the head of a wrapped declaration satisfied
# the function-declaration guard and the global silently exempted itself.
# Comments are now stripped before either the terminator test or the guard
# see it.
PARENCOMMENT="$TMPROOT/parencomment"
make_fixture "$PARENCOMMENT"
cat > "$PARENCOMMENT/engine/include/irreden/parencomment.hpp" <<'EOF'
#pragma once
#include <unordered_map>
namespace IRFixture {
inline std::unordered_map<int, int>  // registry (id -> slot)
    g_parenCommentRegistry;
}
EOF
parencomment_out=$(run_checker "$PARENCOMMENT")
parencomment_rc=$?
assert_eq "1" "$parencomment_rc" \
    "a paren inside a wrapped head line's trailing comment does not exempt the global"
assert_contains "$parencomment_out" "g_parenCommentRegistry" \
    "failure names the paren-comment-wrapped declaration"

# --- usage guard ------------------------------------------------------------
noroot_out=$(cmake -P "$CHECKER" 2>&1)
noroot_rc=$?
assert_eq "1" "$noroot_rc" "missing PROJECT_ROOT exits 1"
assert_contains "$noroot_out" "PROJECT_ROOT is required" \
    "missing PROJECT_ROOT explains itself"


# ===========================================================================
# Checker include-set census (#3118)
# ===========================================================================
#
# Everything above drives the shim and asserts what it FINDS. This section
# asserts what it RUNS. The shim is the only path CI executes — quality.yml,
# the `lint` target's sole route, is retired (#2718) — so a checker wired only
# into `irreden_add_quality_targets` (a `-P` invocation, no shim `include()`)
# ships CI-inert while its rule doc still claims "enforced" (see #2794).
#
# The population is derived from a cmake/run_*check*.cmake glob, never from the
# shim's own includes and never from make_fixture's copy list: a domain computed
# from the thing under test is invisible to both a green run and its positive
# control (#2876). The glob IS the domain, so a checker named outside it stays
# invisible — that is the accepted tradeoff #2876 prescribes, not a defect.

# checker_includes <cmake-file> — one line per include() argument, comments
# stripped and the whole file joined first so a wrapped `include(\n  "...")`
# still reads as one call. Stripping before matching is what makes a
# commented-out include read as absent (the #2899 contract, here in the
# wiring dimension); joining first is what keeps the guard from being
# defeated by reformatting (the #2916 shape, here in the wiring dimension).
checker_includes() {
    awk '
        { sub(/#.*/, ""); buf = buf " " $0 }
        END {
            while (match(buf, /include[ \t]*\([^)]*\)/)) {
                call = substr(buf, RSTART, RLENGTH)
                buf = substr(buf, RSTART + RLENGTH)
                sub(/^include[ \t]*\([ \t]*/, "", call)
                sub(/[ \t]*\)$/, "", call)
                gsub(/[ \t"]/, "", call)
                if (call != "") print call
            }
        }
    ' "$1"
}

# census_population <root> — the basename of every checker the glob finds
# under <root>/cmake, excluding the shim itself. The exclusion is a literal
# one-liner on purpose: #3267 lands a second standalone shim, and the day a
# checker belongs to THAT shim this guard would otherwise demand its include
# in the wrong one. A one-line exclusion is trivial to extend; a derived one
# is not.
census_population() {
    local root="$1" f base
    for f in "$root"/cmake/run_*check*.cmake; do
        [[ -f "$f" ]] || continue
        base=$(basename "$f")
        [[ "$base" == "run_header_checks_standalone.cmake" ]] && continue
        echo "$base"
    done
}

# census_is_vacuous <root> — true when the glob finds no checker besides the
# shim. That shape makes the include-set assertion pass while checking
# nothing, so it has to read as a failure rather than a clean run.
census_is_vacuous() {
    [[ -z "$(census_population "$1")" ]]
}

# census_missing_includes <root> — one line per checker in the population that
# the shim does not directly include(). Empty output means the include set is
# complete.
#
# An include-set guard, not a CMake interpreter: it does not evaluate whether
# an include sits inside a false if() branch. Every shim include today is
# unconditional and side-effecting (no define-then-call indirection), so
# include() presence is a sound proxy for "this checker runs in CI"; the day
# one becomes conditional, that proxy — not this matcher — is what changed.
#
# Matching is on the include argument's BASENAME, at the path boundary — never
# on the ${PROJECT_ROOT} literal the shim happens to spell today. The sibling
# call site in ir_quality_tools.cmake spells the same files
# -P "${PROJECT_SOURCE_DIR}/cmake/<name>", and a shim refactor to
# ${CMAKE_CURRENT_LIST_DIR} is legitimate; pinning the root literal would turn
# this guard red on a correctly-wired checker — the mirror image of the
# formatter-defeatable false negative #2916 records for the header executor.
# The boundary anchor is also what keeps a longer lookalike name
# (run_x_check_v2.cmake) from satisfying run_x_check.cmake.
census_missing_includes() {
    local root="$1"
    # Separate statement on purpose: bash expands every word of a `local`
    # command before any of its assignments take effect, so `local root="$1"
    # shim="$root/..."` reads the CALLER's root, not this one.
    local shim="$root/cmake/run_header_checks_standalone.cmake"
    local -a included=()
    local arg checker base found
    if [[ ! -f "$shim" ]]; then
        echo "MISSING-SHIM:$shim"
        return 0
    fi
    while IFS= read -r arg; do
        [[ -n "$arg" ]] && included+=("${arg##*/}")
    done < <(checker_includes "$shim")
    while IFS= read -r checker; do
        [[ -n "$checker" ]] || continue
        found=0
        for base in ${included[@]+"${included[@]}"}; do
            [[ "$base" == "$checker" ]] && { found=1; break; }
        done
        [[ "$found" -eq 0 ]] && echo "$checker"
    done < <(census_population "$root")
    return 0
}

# assert_census_clean <root> <msg> — the failure branch names the orphans,
# since "which checker is CI-inert" is the whole answer this arm exists to give.
assert_census_clean() {
    local root="$1" msg="$2" missing
    missing=$(census_missing_includes "$root")
    if [[ -z "$missing" ]]; then
        ok "$msg"
    else
        bad "$msg"
        echo "        present in cmake/ but never include()d by the shim —"
        echo "        these run only via the header-checks/lint targets, which have no CI path:"
        printf '%s\n' "$missing" | sed 's/^/          | /'
    fi
}

# make_census_fixture <root> — a hermetic copy of the REAL checker population
# plus the real shim. Copied by glob, not by name list, so a checker added to
# the tree is carried here without editing any fixture-copy list.
make_census_fixture() {
    local root="$1"
    mkdir -p "$root/cmake"
    cp "$SCRIPT_DIR"/cmake/run_*check*.cmake "$root/cmake/"
}

# make_synthetic_census_fixture <root> <shim-body> — two stand-in checkers and
# a shim whose include set the arm dictates. The matcher-semantics arms use
# this rather than mutating a copy of the real shim: the property under test is
# how an include is SPELLED, and a synthetic shim states each spelling outright
# instead of reaching it through a sed rewrite.
make_synthetic_census_fixture() {
    local root="$1" shim_body="$2"
    mkdir -p "$root/cmake"
    : > "$root/cmake/run_alpha_check.cmake"
    : > "$root/cmake/run_beta_check.cmake"
    printf '%s\n' "$shim_body" > "$root/cmake/run_header_checks_standalone.cmake"
}

# --- the source tree's own include set is complete --------------------------
# The guard arm, and the only one that reads the live repo. Every mutation arm
# below works on a temp copy — nothing in this suite writes into cmake/.
census_count=0
while IFS= read -r _census_entry; do
    [[ -n "$_census_entry" ]] && census_count=$((census_count + 1))
done < <(census_population "$SCRIPT_DIR")

if census_is_vacuous "$SCRIPT_DIR"; then
    bad "cmake/run_*check*.cmake finds no checker besides the shim — the glob broke, and the census below would pass vacuously"
else
    ok "census domain is non-empty: $census_count checker(s) besides the shim"
fi
assert_census_clean "$SCRIPT_DIR" \
    "every cmake/run_*check*.cmake is include()d by run_header_checks_standalone.cmake"

# --- a hermetic copy of that same population is clean too -------------------
# Pins that the census reads its <root> argument rather than the repo root it
# was first written against; every arm below depends on that.
CENSUS_CLEAN="$TMPROOT/census-clean"
make_census_fixture "$CENSUS_CLEAN"
assert_census_clean "$CENSUS_CLEAN" "a temp copy of the real cmake/ censuses clean"

# --- a checker born without a shim include is named -------------------------
# The discriminating control. Deleting an existing include is the other
# mutation, but ~39 of the scratch-behaviour arms above fire on it first — this
# shape fires nothing else, so it is the one that proves THIS arm works.
CENSUS_ORPHAN="$TMPROOT/census-orphan"
make_census_fixture "$CENSUS_ORPHAN"
cat > "$CENSUS_ORPHAN/cmake/run_fixture_orphan_check.cmake" <<'EOF'
message(FATAL_ERROR "FIXTURE_ORPHAN_PROBE fired")
EOF
orphan_missing=$(census_missing_includes "$CENSUS_ORPHAN")
assert_contains "$orphan_missing" "run_fixture_orphan_check.cmake" \
    "a checker added to cmake/ with no shim include is named by the census"
assert_absent "$orphan_missing" "run_metal_scratch_consumer_check.cmake" \
    "correctly-wired checkers are not swept up with it"
rm "$CENSUS_ORPHAN/cmake/run_fixture_orphan_check.cmake"
assert_census_clean "$CENSUS_ORPHAN" "removing the injected checker restores a clean census"

# --- a root with no shim at all is reported, not silently clean -------------
CENSUS_NOSHIM="$TMPROOT/census-noshim"
mkdir -p "$CENSUS_NOSHIM/cmake"
cp "$SCRIPT_DIR/cmake/run_metal_kernel_registry_check.cmake" "$CENSUS_NOSHIM/cmake/"
assert_contains "$(census_missing_includes "$CENSUS_NOSHIM")" "MISSING-SHIM" \
    "a checker population with no shim beside it reads as unwired, not as clean"

# --- an empty population reads as vacuous, not as clean ---------------------
CENSUS_EMPTY="$TMPROOT/census-empty"
mkdir -p "$CENSUS_EMPTY/cmake"
cp "$CHECKER" "$CENSUS_EMPTY/cmake/"
if census_is_vacuous "$CENSUS_EMPTY"; then
    ok "a cmake/ holding only the shim is detected as an empty population"
else
    bad "a cmake/ holding only the shim was not detected as an empty population"
fi
assert_eq "" "$(census_missing_includes "$CENSUS_EMPTY")" \
    "the include-set assertion alone would have called that tree clean"

# --- a commented-out include reads as absent --------------------------------
# Same contract as #2899 on the registry side: a disabled wiring line never
# reaches the executed path, so it must not satisfy the guard.
CENSUS_COMMENTED="$TMPROOT/census-commented"
make_synthetic_census_fixture "$CENSUS_COMMENTED" 'include("${PROJECT_ROOT}/cmake/run_alpha_check.cmake")
# include("${PROJECT_ROOT}/cmake/run_beta_check.cmake")'
commented_missing=$(census_missing_includes "$CENSUS_COMMENTED")
assert_contains "$commented_missing" "run_beta_check.cmake" \
    "a commented-out include does not satisfy the census"
assert_absent "$commented_missing" "run_alpha_check.cmake" \
    "the live include in the same shim still satisfies it"

# --- prose mentions and lookalike filenames do not satisfy it ---------------
# The real shim's header comment names all three of its checkers, so a
# substring search over the file is a guaranteed false pass.
CENSUS_LOOKALIKE="$TMPROOT/census-lookalike"
make_synthetic_census_fixture "$CENSUS_LOOKALIKE" '# Delegates to run_alpha_check.cmake and run_beta_check.cmake.
include("${PROJECT_ROOT}/cmake/run_alpha_check.cmake")
include("${PROJECT_ROOT}/cmake/run_beta_check_v2.cmake")'
lookalike_missing=$(census_missing_includes "$CENSUS_LOOKALIKE")
assert_contains "$lookalike_missing" "run_beta_check.cmake" \
    "a prose mention plus a longer lookalike include does not satisfy the census"
assert_absent "$lookalike_missing" "run_alpha_check.cmake" \
    "the prose mention is not what cleared alpha — its real include is"

# --- an alternate root spelling is still a direct include -------------------
# Guards the mirror-image failure: a false POSITIVE that turns red on a
# correctly-wired checker the day someone refactors the shim's path variable.
CENSUS_ALTROOT="$TMPROOT/census-altroot"
make_synthetic_census_fixture "$CENSUS_ALTROOT" 'include("${CMAKE_CURRENT_LIST_DIR}/run_alpha_check.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/run_beta_check.cmake")'
assert_census_clean "$CENSUS_ALTROOT" \
    "\${CMAKE_CURRENT_LIST_DIR} spelling still reads as a direct include"

# --- an include wrapped across lines is still a direct include --------------
# No cmake formatter is configured in-tree today, so this is insurance rather
# than a live hazard — but it is the exact shape that made the header executor
# formatter-defeatable (#2916), one artifact over.
CENSUS_WRAPPED="$TMPROOT/census-wrapped"
make_synthetic_census_fixture "$CENSUS_WRAPPED" 'include("${PROJECT_ROOT}/cmake/run_alpha_check.cmake")
include(
    "${PROJECT_ROOT}/cmake/run_beta_check.cmake")'
assert_census_clean "$CENSUS_WRAPPED" \
    "an include() wrapped across lines still reads as a direct include"

# --- header-checks.yml keeps the census reachable ---------------------------
# The census reads cmake/ and never .github/workflows/, so dropping the checker
# glob from either paths: block would silently un-cover every new checker and
# nothing would go red — the gap this ticket closes, relocated one artifact
# over. This arm makes that a ratchet instead of a one-time merge-day check.
HEADER_CHECKS_WORKFLOW="$SCRIPT_DIR/.github/workflows/header-checks.yml"
SYNTHETIC_CHECKER_PATH="cmake/run_zz_synthetic_check.cmake"

# workflow_paths_globs <file> <section> — the paths: entries under the named
# on: sub-block, unquoted. Same awk shape as test_workflow_paths_sync.sh: stop
# at the first line that is not a "      - " item, so a sibling top-level key
# can never be misread as workflow content.
workflow_paths_globs() {
    awk -v section="$2" '
        $0 ~ "^  " section ":" { in_section=1; next }
        in_section && /^  [a-zA-Z_]+:/ { in_section=0 }
        in_section && /^    paths:/ { in_paths=1; next }
        in_section && in_paths && /^      - / {
            line = $0
            sub(/^      - /, "", line)
            gsub(/"/, "", line)
            gsub(/\047/, "", line)
            print line
            next
        }
        in_section && in_paths { in_paths=0 }
    ' "$1"
}

# workflow_covers_new_checker <file> <section> — true when some paths: entry in
# that block, read as a glob, matches a checker filename that does not exist
# yet. A filter enumerating today's names matches nothing here; the glob does.
workflow_covers_new_checker() {
    local file="$1" section="$2" entry
    while IFS= read -r entry; do
        [[ -z "$entry" ]] && continue
        # shellcheck disable=SC2053  # glob match is the point
        [[ "$SYNTHETIC_CHECKER_PATH" == $entry ]] && return 0
    done < <(workflow_paths_globs "$file" "$section")
    return 1
}

if [[ ! -f "$HEADER_CHECKS_WORKFLOW" ]]; then
    bad "header-checks.yml not found at $HEADER_CHECKS_WORKFLOW"
else
    for census_section in push pull_request; do
        if workflow_covers_new_checker "$HEADER_CHECKS_WORKFLOW" "$census_section"; then
            ok "header-checks.yml ${census_section}: paths: covers a checker filename that does not exist yet"
        else
            bad "header-checks.yml ${census_section}: paths: matches no new checker name — a checker added to cmake/ would not trigger the job that runs this census"
        fi
    done

    # Negative control: strip the glob entries and the arm above must stop
    # passing. Without this, a filter listing every current name by hand would
    # look identical to the glob on the real file only by accident.
    CENSUS_WORKFLOW_STRIPPED="$TMPROOT/census-workflow-stripped.yml"
    awk '!/^      - .*cmake\/run_/ { print }' "$HEADER_CHECKS_WORKFLOW" \
        > "$CENSUS_WORKFLOW_STRIPPED"
    for census_section in push pull_request; do
        if workflow_covers_new_checker "$CENSUS_WORKFLOW_STRIPPED" "$census_section"; then
            bad "negative control: a header-checks.yml with no cmake/run_* filter still read as covered — the arm above cannot fail"
        else
            ok "negative control: stripping the cmake/run_* filter makes the ${census_section} arm fail"
        fi
    done
fi

summarize "run_header_checks_standalone tests"

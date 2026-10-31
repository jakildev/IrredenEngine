#ifndef SUN_FACE_QUERY_LAYOUT_H
#define SUN_FACE_QUERY_LAYOUT_H

#include <irreden/render/sun_shadow_cascade.hpp>

#include <cstdint>

namespace IRPrefab::SunShadow {

// Shared with ir_sun_face_query_layout in both shader backends. Consumers are
// the sun-map allocation/clear, rigid source bake and surface receiver lookup.
//
// Everything up to the record pool has a fixed offset. The record pool and the
// hierarchy that indexes it scale with the allocated record capacity, which
// the bake system grows between frames; SourceFaceLayout derives the tail
// offsets from that capacity. The kSourceFace*Offset constants describe the
// default allocation and stay valid for a buffer created at
// kSourceFaceCapacity records. Metal reads the live capacity from the header
// word at kSourceFaceCapacityOffset; GLSL derives it from the bound length.
constexpr std::uint32_t kSourceFaceMapDimension = kSunShadowMapDim;
constexpr std::uint32_t kSourceFaceCascadeCount = kSunShadowCascadeCount;
constexpr std::uint32_t kSourceFaceTileEdge = 8u;
constexpr std::uint32_t kSourceFaceTilesPerAxis = kSourceFaceMapDimension / kSourceFaceTileEdge;
constexpr std::uint32_t kSourceFaceTileCount =
    kSourceFaceCascadeCount * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis;
constexpr std::uint32_t kSourceFaceTileCapacity = 64u;
constexpr std::uint32_t kSourceFaceTileQueryCapacity = 256u;
constexpr std::uint32_t kSourceFaceSpillCapacity = 262144u;
constexpr std::uint32_t kSourceFaceSpillNodeWords = 2u;
constexpr std::uint32_t kSourceFaceCapacity = 65536u;
// Largest record pool the frame-latent growth may allocate by default: the
// largest pool whose exact queries stayed inside the frame budget on the dense
// perf grid. A workload whose demand exceeds it keeps losing records and
// reports the exhaustion; a creation raises it through configureRecordPool.
constexpr std::uint32_t kSourceFaceCapacityCeiling = 131072u;
// Largest pool the dispatched hierarchy passes can root.
constexpr std::uint32_t kSourceFaceCapacityDispatchLimit = 16777216u;
constexpr std::uint32_t kSourceFaceRecordWords = 10u;
constexpr std::uint32_t kSourceFaceFallbackOffset =
    kSourceFaceCascadeCount * kSourceFaceMapDimension * kSourceFaceMapDimension;
// Per-texel depth-slab extrema of the primary layer: kSunDepthSlabSlots slots
// of (min, max) packed words, each slot owning one slab of kSunDepthSlabWidthUnits
// depth units. The slab width equals the receiver's shadow throw, so a
// throw-wide window is answered exactly by the max of the slab holding its far
// end and the min of the next slab. Slot 0's max word carries the sticky
// overflow bit once a texel saw more distinct slabs than it has slots; the
// receiver then answers from the nearest word alone.
//
// Zero slots compiles the layer out: no words, and every primary write and
// receiver tap byte-identical to the nearest-only bake. A creation that needs
// raster casters retained behind a distant one opts in with at least three
// slots (a far caster, a lower blocker and the receiver's own surface each
// take one), in this constant and both shader twins; the cost that keeps the
// default at zero is in docs/design/sun-face-index-capacity.md.
constexpr std::uint32_t kSunDepthSlabSlots = 0u;
constexpr std::uint32_t kSunDepthSlabWords = 2u;
constexpr std::uint32_t kSunDepthSlabTexelWords = kSunDepthSlabSlots * kSunDepthSlabWords;
constexpr std::uint32_t kSunDepthSlabOffset = 2u * kSourceFaceFallbackOffset;
constexpr std::uint32_t kSunDepthSlabLayerWords =
    kSourceFaceFallbackOffset * kSunDepthSlabTexelWords;
constexpr std::uint32_t kSunDepthSlabWidthUnits = 64u;
constexpr std::uint32_t kSunDepthSlabQuantized = 65536u;
constexpr std::uint32_t kSunDepthSlabOverflow = 0x80000000u;
constexpr std::uint32_t kSourceFaceHeaderOffset = kSunDepthSlabOffset + kSunDepthSlabLayerWords;
// Header words: requested record count, then the allocated record capacity.
constexpr std::uint32_t kSourceFaceCapacityOffset = kSourceFaceHeaderOffset + 1u;
constexpr std::uint32_t kSourceFaceHeaderWords = 2u;
constexpr std::uint32_t kSourceFaceTileOffset = kSourceFaceHeaderOffset + kSourceFaceHeaderWords;
constexpr std::uint32_t kSourceFaceRecordOffset =
    kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
constexpr std::uint32_t kSourceFaceHierarchyLeafSize = 8u;
constexpr std::uint32_t kSourceFaceHierarchyNodeWords = 8u;
constexpr std::uint32_t kSourceFaceHierarchyWorkgroupSize = 128u;
// Words after the record pool that do not scale with it: the spill counter,
// spill heads and nodes, and the two hierarchy flags.
constexpr std::uint32_t kSourceFaceTailFixedWords =
    1u + kSourceFaceTileCount + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords + 2u;
// Each record owns ten geometry words and two hierarchy node words.
constexpr std::uint32_t kSourceFaceWordsPerRecord =
    kSourceFaceRecordWords + 2u * kSourceFaceHierarchyNodeWords / kSourceFaceHierarchyLeafSize;
// Dispatched passes cover kSourceFaceHierarchyWorkgroupSize^passes leaves; two
// passes are the floor so the default allocation keeps its dispatch sequence.
constexpr int kSourceFaceHierarchyMinBuildPasses = 2;
constexpr int kSourceFaceHierarchyMaxBuildPasses = 3;
// Sticky tile-count bit: a failed record reservation left overlapping geometry outside the pool.
constexpr std::uint32_t kSourceFaceTileRecordLost = 0x80000000u;

// Standalone on purpose: the shader-contract harnesses compile this header
// as C++17 without the engine, so the power-of-two arithmetic stays local
// rather than using IRMath or <bit>.
constexpr bool sourceFaceCapacityValid(std::uint32_t capacity) {
    return capacity >= kSourceFaceHierarchyLeafSize && (capacity & (capacity - 1u)) == 0u &&
           capacity <= kSourceFaceCapacityDispatchLimit;
}

struct SourceFaceLayout {
    std::uint32_t capacity_ = kSourceFaceCapacity;

    constexpr std::uint32_t spillCounterOffset() const {
        return kSourceFaceRecordOffset + capacity_ * kSourceFaceRecordWords;
    }
    constexpr std::uint32_t spillHeadOffset() const {
        return spillCounterOffset() + 1u;
    }
    constexpr std::uint32_t spillNodeOffset() const {
        return spillHeadOffset() + kSourceFaceTileCount;
    }
    constexpr std::uint32_t hierarchyNeededOffset() const {
        return spillNodeOffset() + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords;
    }
    constexpr std::uint32_t hierarchyReadyOffset() const {
        return hierarchyNeededOffset() + 1u;
    }
    constexpr std::uint32_t hierarchyOffset() const {
        return hierarchyReadyOffset() + 1u;
    }
    constexpr std::uint32_t hierarchyMaxLeaves() const {
        return capacity_ / kSourceFaceHierarchyLeafSize;
    }
    constexpr std::uint32_t bufferWords() const {
        return hierarchyOffset() + 2u * hierarchyMaxLeaves() * kSourceFaceHierarchyNodeWords;
    }
    // The shader twins recover the capacity from the bound length (GLSL) or the
    // header word (Metal); the length form must invert bufferWords exactly.
    static constexpr std::uint32_t capacityForBufferWords(std::uint32_t words) {
        return (words - kSourceFaceRecordOffset - kSourceFaceTailFixedWords) /
               kSourceFaceWordsPerRecord;
    }
    constexpr std::uint32_t bufferBytes() const {
        return bufferWords() * static_cast<std::uint32_t>(sizeof(std::uint32_t));
    }
    // Cooperative passes whose workgroup-wide subtrees reach one root.
    constexpr int buildPasses() const {
        int passes = 1;
        std::uint32_t covered = kSourceFaceHierarchyWorkgroupSize;
        while (covered < hierarchyMaxLeaves()) {
            covered *= kSourceFaceHierarchyWorkgroupSize;
            ++passes;
        }
        return passes < kSourceFaceHierarchyMinBuildPasses ? kSourceFaceHierarchyMinBuildPasses
                                                           : passes;
    }
    // Workgroups a pass dispatches: one per subtree the pass can root.
    constexpr std::uint32_t buildGroups(int pass) const {
        std::uint32_t groups = hierarchyMaxLeaves();
        for (int level = 0; level <= pass; ++level) {
            groups = (groups + kSourceFaceHierarchyWorkgroupSize - 1u) /
                     kSourceFaceHierarchyWorkgroupSize;
        }
        return groups < 1u ? 1u : groups;
    }
};

// Default-allocation offsets, kept for consumers and tests that address the
// buffer a kSourceFaceCapacity allocation produces.
constexpr SourceFaceLayout kSourceFaceDefaultLayout{kSourceFaceCapacity};
constexpr std::uint32_t kSourceFaceSpillCounterOffset =
    kSourceFaceDefaultLayout.spillCounterOffset();
constexpr std::uint32_t kSourceFaceSpillHeadOffset = kSourceFaceDefaultLayout.spillHeadOffset();
constexpr std::uint32_t kSourceFaceSpillNodeOffset = kSourceFaceDefaultLayout.spillNodeOffset();
constexpr std::uint32_t kSourceFaceHierarchyNeededOffset =
    kSourceFaceDefaultLayout.hierarchyNeededOffset();
constexpr std::uint32_t kSourceFaceHierarchyReadyOffset =
    kSourceFaceDefaultLayout.hierarchyReadyOffset();
constexpr std::uint32_t kSourceFaceHierarchyOffset = kSourceFaceDefaultLayout.hierarchyOffset();
constexpr std::uint32_t kSourceFaceHierarchyMaxLeaves =
    kSourceFaceDefaultLayout.hierarchyMaxLeaves();
constexpr std::uint32_t kSourceFaceBufferWords = kSourceFaceDefaultLayout.bufferWords();
constexpr int kSourceFaceHierarchyLevels = 14;
constexpr int kSourceFaceHierarchyBuildPasses = kSourceFaceDefaultLayout.buildPasses();

// Capacity a completed demand sample asks for: the smallest valid capacity
// holding every requested record, never below the current one and never above
// the ceiling. Equal to @p current when the sample fits or the ceiling binds.
constexpr std::uint32_t
grownSourceFaceCapacity(std::uint32_t requested, std::uint32_t current, std::uint32_t ceiling) {
    if (requested <= current || current >= ceiling) {
        return current;
    }
    std::uint32_t grown = current;
    while (grown < requested && grown < ceiling) {
        grown *= 2u;
    }
    return grown > ceiling ? ceiling : grown;
}

static_assert(
    sourceFaceCapacityValid(kSourceFaceCapacity), "Default capacity must be a power of two"
);
static_assert(
    sourceFaceCapacityValid(kSourceFaceCapacityCeiling), "Ceiling must be a power of two"
);
static_assert(
    kSourceFaceCapacityCeiling >= kSourceFaceCapacity, "Ceiling must hold the default pool"
);
static_assert(kSourceFaceHierarchyWorkgroupSize > 0u, "Hierarchy workgroups must contain lanes");
static_assert(
    kSourceFaceHierarchyBuildPasses == 2, "Default allocation keeps its two-pass schedule"
);
static_assert(
    SourceFaceLayout{kSourceFaceCapacityDispatchLimit}.buildPasses() ==
            kSourceFaceHierarchyMaxBuildPasses &&
        SourceFaceLayout{2u * kSourceFaceCapacityDispatchLimit}.buildPasses() >
            kSourceFaceHierarchyMaxBuildPasses,
    "The dispatch limit is the largest pool the pass buffers can root"
);
static_assert(
    kSourceFaceCapacityDispatchLimit < kSourceFaceTileRecordLost,
    "Every dispatchable pool stays below the lost-record bit"
);
static_assert(
    (kSourceFaceHierarchyWorkgroupSize & (kSourceFaceHierarchyWorkgroupSize - 1u)) == 0u,
    "Hierarchy workgroup width must be a power of two"
);
static_assert(kSourceFaceHierarchyLeafSize > 0u, "Hierarchy leaves must hold records");
static_assert(
    kSourceFaceHierarchyMaxLeaves == (1u << (kSourceFaceHierarchyLevels - 1)),
    "Hierarchy levels must reduce a power-of-two leaf allocation to one root"
);
static_assert(
    kSunDepthSlabQuantized == kSunDepthSlabWidthUnits * 1024u,
    "The slab quantum is the slab width in packed sun-depth units"
);
static_assert(
    kSunDepthSlabOverflow > ((1u << 20) << 8 | 0xFFu),
    "The overflow bit must sit above every packed depth word"
);
static_assert(
    kSunDepthSlabOffset == 2u * kSourceFaceFallbackOffset &&
        kSourceFaceHeaderOffset == kSunDepthSlabOffset + kSunDepthSlabLayerWords,
    "Slab extrema sit between the sampled layers and the index header"
);

static_assert(
    kSourceFaceTileQueryCapacity > kSourceFaceTileCapacity,
    "Spill queries must extend the inline tile capacity"
);
static_assert(
    kSourceFaceTileCount * (kSourceFaceTileQueryCapacity - kSourceFaceTileCapacity) < 0xFFFFFFFFu,
    "Spill allocation attempts must not wrap the counter"
);
static_assert(
    kSourceFaceCapacityCeiling <= kSourceFaceCapacityDispatchLimit,
    "Retained-record appends to one tile must not reach the lost-record bit"
);
static_assert(
    kSourceFaceMapDimension % kSourceFaceTileEdge == 0u,
    "Source face tiles must divide each shadow cascade"
);
static_assert(
    kSourceFaceTileCount <= kSourceFaceFallbackOffset,
    "The shadow clear dispatch must cover every source tile counter"
);
static_assert(
    kSourceFaceBufferWords > kSourceFaceRecordOffset,
    "Source face storage must contain the bounded record pool"
);
static_assert(
    SourceFaceLayout::capacityForBufferWords(kSourceFaceBufferWords) == kSourceFaceCapacity &&
        SourceFaceLayout::capacityForBufferWords(
            SourceFaceLayout{kSourceFaceCapacityCeiling}.bufferWords()
        ) == kSourceFaceCapacityCeiling,
    "The bound length must identify the record capacity"
);
static_assert(
    grownSourceFaceCapacity(
        kSourceFaceCapacity + 1u, kSourceFaceCapacity, kSourceFaceCapacityCeiling
    ) == 2u * kSourceFaceCapacity,
    "One record past the pool doubles it"
);

} // namespace IRPrefab::SunShadow

#endif

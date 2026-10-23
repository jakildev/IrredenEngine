#ifndef SUN_FACE_QUERY_LAYOUT_H
#define SUN_FACE_QUERY_LAYOUT_H

#include <irreden/render/sun_shadow_cascade.hpp>

#include <cstdint>

namespace IRPrefab::SunShadow {

// Shared with ir_sun_face_query_layout in both shader backends. Consumers are
// the sun-map allocation/clear, rigid source bake and surface receiver lookup.
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
constexpr std::uint32_t kSourceFaceRecordWords = 10u;
constexpr std::uint32_t kSourceFaceFallbackOffset =
    kSourceFaceCascadeCount * kSourceFaceMapDimension * kSourceFaceMapDimension;
constexpr std::uint32_t kSourceFaceHeaderOffset = 2u * kSourceFaceFallbackOffset;
constexpr std::uint32_t kSourceFaceTileOffset = kSourceFaceHeaderOffset + 1u;
constexpr std::uint32_t kSourceFaceRecordOffset =
    kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
constexpr std::uint32_t kSourceFaceSpillCounterOffset =
    kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;
constexpr std::uint32_t kSourceFaceSpillHeadOffset = kSourceFaceSpillCounterOffset + 1u;
constexpr std::uint32_t kSourceFaceSpillNodeOffset =
    kSourceFaceSpillHeadOffset + kSourceFaceTileCount;
constexpr std::uint32_t kSourceFaceHierarchyNeededOffset =
    kSourceFaceSpillNodeOffset + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords;
constexpr std::uint32_t kSourceFaceHierarchyReadyOffset = kSourceFaceHierarchyNeededOffset + 1u;
constexpr std::uint32_t kSourceFaceHierarchyOffset = kSourceFaceHierarchyReadyOffset + 1u;
constexpr std::uint32_t kSourceFaceHierarchyLeafSize = 8u;
constexpr std::uint32_t kSourceFaceHierarchyMaxLeaves =
    kSourceFaceCapacity / kSourceFaceHierarchyLeafSize;
constexpr std::uint32_t kSourceFaceHierarchyNodeWords = 8u;
constexpr std::uint32_t kSourceFaceBufferWords =
    kSourceFaceHierarchyOffset + 2u * kSourceFaceHierarchyMaxLeaves * kSourceFaceHierarchyNodeWords;

constexpr int kSourceFaceHierarchyLevels = 14;
constexpr int kSourceFaceHierarchyBuildPasses = 2;
constexpr std::uint32_t kSourceFaceHierarchyWorkgroupSize = 128u;
static_assert(kSourceFaceHierarchyWorkgroupSize > 0u, "Hierarchy workgroups must contain lanes");
static_assert(
    kSourceFaceHierarchyBuildPasses == 2, "Hierarchy construction requires subtree and root passes"
);
static_assert(
    (kSourceFaceHierarchyWorkgroupSize & (kSourceFaceHierarchyWorkgroupSize - 1u)) == 0u,
    "Hierarchy workgroup width must be a power of two"
);
static_assert(
    kSourceFaceHierarchyMaxLeaves <=
        kSourceFaceHierarchyWorkgroupSize * kSourceFaceHierarchyWorkgroupSize,
    "Two hierarchy passes must cover the whole leaf allocation"
);
static_assert(kSourceFaceHierarchyLeafSize > 0u, "Hierarchy leaves must hold records");
static_assert(
    kSourceFaceCapacity % kSourceFaceHierarchyLeafSize == 0u,
    "Hierarchy leaf allocation must cover the whole record pool"
);
static_assert(
    kSourceFaceHierarchyMaxLeaves == (1u << (kSourceFaceHierarchyLevels - 1)),
    "Hierarchy levels must reduce a power-of-two leaf allocation to one root"
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

} // namespace IRPrefab::SunShadow

#endif

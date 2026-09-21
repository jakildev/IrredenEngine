#ifndef SUN_FACE_QUERY_LAYOUT_H
#define SUN_FACE_QUERY_LAYOUT_H

#include <cstdint>

namespace IRPrefab::SunShadow {

// Shared with ir_sun_face_query_layout in both shader backends. Consumers are
// the sun-map allocation/clear, rigid source bake and surface receiver lookup.
constexpr std::uint32_t kSourceFaceMapDimension = 1024u;
constexpr std::uint32_t kSourceFaceCascadeCount = 2u;
constexpr std::uint32_t kSourceFaceTileEdge = 8u;
constexpr std::uint32_t kSourceFaceTilesPerAxis = kSourceFaceMapDimension / kSourceFaceTileEdge;
constexpr std::uint32_t kSourceFaceTileCount =
    kSourceFaceCascadeCount * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis;
constexpr std::uint32_t kSourceFaceTileCapacity = 64u;
constexpr std::uint32_t kSourceFaceCapacity = 65536u;
constexpr std::uint32_t kSourceFaceRecordWords = 9u;
constexpr std::uint32_t kSourceFaceFallbackOffset =
    kSourceFaceCascadeCount * kSourceFaceMapDimension * kSourceFaceMapDimension;
constexpr std::uint32_t kSourceFaceHeaderOffset = 2u * kSourceFaceFallbackOffset;
constexpr std::uint32_t kSourceFaceTileOffset = kSourceFaceHeaderOffset + 1u;
constexpr std::uint32_t kSourceFaceRecordOffset =
    kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
constexpr std::uint32_t kSourceFaceBufferWords =
    kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;

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

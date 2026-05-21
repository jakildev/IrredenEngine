#ifndef IRREDEN_PREFAB_WORLD_CHUNK_COORD_H
#define IRREDEN_PREFAB_WORLD_CHUNK_COORD_H

// Chunk identity + addressing for the streamed world (Epic E, design at
// docs/design/world-streaming.md §"Topic 1 — Chunk identity + addressing").
//
// A chunk is identified by its chunk-coord ivec3 c. It occupies the
// world-space half-open voxel cube [c * kChunkSize, (c + 1) * kChunkSize).
// Identity is decoupled from residency — a chunk-coord exists whether or
// not the chunk is currently resident in memory.
//
// ChunkKey packs three signed-16-bit axes into a 64-bit key (covers
// ±32768 chunks per axis = ±1048576 voxels per axis). The 16 high bits
// are reserved.

#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>

#include <cstdint>

namespace IRPrefab::Chunk {

/// 64-bit packed chunk identifier — used as the map key for the residency
/// set and as the on-disk filename suffix. Three int16 axes laid out as
/// x in bits [0,16), y in [16,32), z in [32,48); bits [48,64) reserved.
using ChunkKey = std::uint64_t;

namespace detail {

constexpr int kChunkEdge = static_cast<int>(IRConstants::kChunkSize.x);
static_assert(
    IRConstants::kChunkSize.x == IRConstants::kChunkSize.y &&
        IRConstants::kChunkSize.y == IRConstants::kChunkSize.z,
    "ChunkCoord math assumes a cubic chunk; if kChunkSize ever becomes "
    "non-cubic, every helper below needs per-axis sizes."
);

// Signed floor-divide by a positive size. C++ integer division truncates
// toward zero — for negative numerators we need floor (toward -infinity)
// so a world voxel at -1 lands in chunk -1, not chunk 0.
constexpr int floorDivBySize(int n, int size) {
    int q = n / size;
    int r = n % size;
    if (r != 0 && n < 0) {
        return q - 1;
    }
    return q;
}

} // namespace detail

/// World-voxel-space integer position → owning chunk coord. Floors toward
/// -infinity so negative voxels classify into the right chunk.
constexpr IRMath::ivec3 worldToChunk(IRMath::ivec3 worldVoxel) {
    return IRMath::ivec3{
        detail::floorDivBySize(worldVoxel.x, detail::kChunkEdge),
        detail::floorDivBySize(worldVoxel.y, detail::kChunkEdge),
        detail::floorDivBySize(worldVoxel.z, detail::kChunkEdge)
    };
}

/// Origin (min corner, in world voxels) of the given chunk.
constexpr IRMath::ivec3 chunkOriginVoxel(IRMath::ivec3 chunkCoord) {
    return IRMath::ivec3{
        chunkCoord.x * detail::kChunkEdge,
        chunkCoord.y * detail::kChunkEdge,
        chunkCoord.z * detail::kChunkEdge
    };
}

/// Center (in world-space float voxels) of the given chunk. Used by the
/// residency manager's distance-to-camera sort.
constexpr IRMath::vec3 chunkCenterWorld(IRMath::ivec3 chunkCoord) {
    return IRMath::vec3(chunkOriginVoxel(chunkCoord)) +
           IRMath::vec3(IRConstants::kChunkSize) * 0.5f;
}

/// Pack a chunk coord into a 64-bit key. Axes outside the int16 range
/// (±32768) wrap silently — the key fabric assumes ±32768 chunks per
/// axis suffices for any current product. The free 16 high bits are
/// reserved.
constexpr ChunkKey pack(IRMath::ivec3 chunkCoord) {
    return (static_cast<ChunkKey>(static_cast<std::uint16_t>(chunkCoord.x))) |
           (static_cast<ChunkKey>(static_cast<std::uint16_t>(chunkCoord.y)) << 16) |
           (static_cast<ChunkKey>(static_cast<std::uint16_t>(chunkCoord.z)) << 32);
}

/// Unpack a ChunkKey back to a chunk coord. Reverses pack(); sign-extends
/// each int16 axis back to int.
constexpr IRMath::ivec3 unpack(ChunkKey key) {
    return IRMath::ivec3{
        static_cast<int>(static_cast<std::int16_t>(key & 0xFFFF)),
        static_cast<int>(static_cast<std::int16_t>((key >> 16) & 0xFFFF)),
        static_cast<int>(static_cast<std::int16_t>((key >> 32) & 0xFFFF))
    };
}

} // namespace IRPrefab::Chunk

#endif /* IRREDEN_PREFAB_WORLD_CHUNK_COORD_H */

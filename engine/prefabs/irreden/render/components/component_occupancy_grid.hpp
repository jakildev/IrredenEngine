#ifndef COMPONENT_OCCUPANCY_GRID_H
#define COMPONENT_OCCUPANCY_GRID_H

// Camera-independent 3D occupancy bitfield mirroring the solid-voxel set
// of a canvas. Consumed by lighting phases (AO, sun shadows, flood-fill,
// fog-of-war) that need full-volume access rather than only the
// camera-facing winner recorded in the distance texture. Attached opt-in
// alongside `C_VoxelPool` so lighting systems iterate only the canvases
// that asked for it.
//
// Coordinates are in world-voxel space (one bit per integer cell),
// centered so `(0,0,0)` lands in the middle of the bitfield. Voxels
// outside `[-halfExtent, +halfExtent)` are dropped. The layout is
// `[z][y][x]` row-major with LSB = cell 0, matching std430 for a GLSL
// `uint[]` read as a 1D array.

#include <irreden/ir_math.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace IRMath;

namespace IRComponents {

constexpr int kOccupancySpatialChunkSize = 32;

struct C_OccupancyGrid {
    C_OccupancyGrid() = default;

    explicit C_OccupancyGrid(int sizeVoxels)
        : m_sizeVoxels{roundedSize(sizeVoxels)} {

        const std::size_t totalBits =
            static_cast<std::size_t>(m_sizeVoxels) *
            static_cast<std::size_t>(m_sizeVoxels) *
            static_cast<std::size_t>(m_sizeVoxels);
        m_bitfield.assign(totalBits / 32u, 0u);

        const int cpa = chunksPerAxis();
        const std::size_t chunkCount =
            static_cast<std::size_t>(cpa) *
            static_cast<std::size_t>(cpa) *
            static_cast<std::size_t>(cpa);
        m_chunkDirty.assign(chunkCount, true);
        m_anyDirty = true;
    }

    int sizeVoxels() const { return m_sizeVoxels; }
    int halfExtent() const { return m_sizeVoxels / 2; }
    int chunksPerAxis() const { return m_sizeVoxels / kOccupancySpatialChunkSize; }

    const std::vector<std::uint32_t> &bitfield() const { return m_bitfield; }
    std::vector<std::uint32_t> &bitfield() { return m_bitfield; }

    std::size_t bitfieldByteSize() const {
        return m_bitfield.size() * sizeof(std::uint32_t);
    }

    bool isAnyDirty() const { return m_anyDirty; }

    void markAllDirty() {
        std::fill(m_chunkDirty.begin(), m_chunkDirty.end(), true);
        m_anyDirty = true;
    }

    bool isChunkDirty(int cx, int cy, int cz) const {
        return m_chunkDirty[chunkIndex(cx, cy, cz)];
    }

    void clearChunkDirty(int cx, int cy, int cz) {
        m_chunkDirty[chunkIndex(cx, cy, cz)] = false;
    }

    void clearAllDirty() {
        std::fill(m_chunkDirty.begin(), m_chunkDirty.end(), false);
        m_anyDirty = false;
    }

    /// Clear every bit in the currently-dirty chunks. Prep step before a
    /// rebuild so `setBit` accumulates into a clean slate.
    void clearBitsInDirtyChunks() {
        const int cpa = chunksPerAxis();
        for (int cz = 0; cz < cpa; ++cz) {
            for (int cy = 0; cy < cpa; ++cy) {
                for (int cx = 0; cx < cpa; ++cx) {
                    if (!isChunkDirty(cx, cy, cz)) continue;
                    clearChunkBits(cx, cy, cz);
                }
            }
        }
    }

    bool inBounds(int wx, int wy, int wz) const {
        const int he = halfExtent();
        return wx >= -he && wx < he &&
               wy >= -he && wy < he &&
               wz >= -he && wz < he;
    }

    void setBit(int wx, int wy, int wz) {
        if (!inBounds(wx, wy, wz)) return;
        const std::size_t flat = flatIndex(wx, wy, wz);
        m_bitfield[flat >> 5u] |= (1u << (flat & 31u));
    }

    bool getBit(int wx, int wy, int wz) const {
        if (!inBounds(wx, wy, wz)) return false;
        const std::size_t flat = flatIndex(wx, wy, wz);
        return (m_bitfield[flat >> 5u] >> (flat & 31u)) & 1u;
    }

  private:
    int m_sizeVoxels = 0;
    std::vector<std::uint32_t> m_bitfield;
    std::vector<std::uint8_t> m_chunkDirty;
    bool m_anyDirty = false;

    /// Round up to 2× chunk size so `halfExtent` is always a multiple of
    /// 32 — lets `clearChunkBits` do aligned uint32 stores per x-row
    /// without a fallback path.
    static int roundedSize(int v) {
        const int alignment = 2 * kOccupancySpatialChunkSize;
        return divCeil(v, alignment) * alignment;
    }

    std::size_t flatIndex(int wx, int wy, int wz) const {
        const int he = halfExtent();
        const std::size_t x = static_cast<std::size_t>(wx + he);
        const std::size_t y = static_cast<std::size_t>(wy + he);
        const std::size_t z = static_cast<std::size_t>(wz + he);
        const std::size_t size = static_cast<std::size_t>(m_sizeVoxels);
        return (z * size + y) * size + x;
    }

    std::size_t chunkIndex(int cx, int cy, int cz) const {
        const std::size_t cpa = static_cast<std::size_t>(chunksPerAxis());
        return (static_cast<std::size_t>(cz) * cpa +
                static_cast<std::size_t>(cy)) * cpa +
               static_cast<std::size_t>(cx);
    }

    /// Clear one spatial chunk. `roundedSize` guarantees
    /// `halfExtent % 32 == 0`, so each x-row inside the chunk is exactly
    /// one uint32 word, aligned — a single store clears 32 cells.
    void clearChunkBits(int cx, int cy, int cz) {
        const int cs = kOccupancySpatialChunkSize;
        const int he = halfExtent();
        const int xMin = cx * cs - he;
        const int yMin = cy * cs - he;
        const int zMin = cz * cs - he;
        for (int z = zMin; z < zMin + cs; ++z) {
            for (int y = yMin; y < yMin + cs; ++y) {
                const std::size_t rowStart = flatIndex(xMin, y, z);
                m_bitfield[rowStart >> 5u] = 0u;
            }
        }
    }
};

} // namespace IRComponents

#endif /* COMPONENT_OCCUPANCY_GRID_H */

#ifndef FIELD_CHUNK_PERSISTENCE_H
#define FIELD_CHUNK_PERSISTENCE_H

// Region files for a 2D chunked field layer: one file per 16×16 field chunks
// (512×512 cells). The path, the `IRFD` container and the failure contract are
// docs/design/fog-of-war-world-field.md D4:
//
//     <saveRoot>/fields/<layer>/<floorDiv(rx,64)>/<floorDiv(ry,64)>/
//         <sx><10-digit |rx|>_<sy><10-digit |ry|>.irfield
//
// `loadRegion` is the only probe a field makes: one quiet `fopen`, and a
// missing file is a silent absence. `regionExists` is for tests and tools.

#include <irreden/ir_math.hpp>
#include <irreden/spatial/chunked_field.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace IRWorld {

constexpr int kFieldRegionEdgeChunks = 16;
constexpr int kFieldRegionChunks = kFieldRegionEdgeChunks * kFieldRegionEdgeChunks;
constexpr int kFieldRegionMaskBytes = kFieldRegionChunks / 8;

/// One region's present field chunks. Bit `i = ly * 16 + lx` of `mask_`
/// (byte `i / 8`, bit `i % 8`) marks region-local field chunk `(lx, ly)`
/// present; `cells_` packs the present field chunks in ascending bit order,
/// each `kFieldChunkCells * bytesPerCell` bytes, row-major.
struct FieldRegion {
    std::array<std::uint8_t, kFieldRegionMaskBytes> mask_{};
    std::vector<std::uint8_t> cells_;

    bool hasChunk(int bit) const {
        return ((mask_[static_cast<std::size_t>(bit / 8)] >> (bit % 8)) & 1u) != 0;
    }

    void setChunk(int bit) {
        mask_[static_cast<std::size_t>(bit / 8)] |= static_cast<std::uint8_t>(1u << (bit % 8));
    }

    int chunkCount() const;
};

class FieldChunkDiskPersistence {
  public:
    /// `nullopt` for an empty @p saveRoot, a @p layer outside
    /// `[a-z][a-z0-9_]{0,31}`, or @p bytesPerCell outside `[1, 255]`.
    static std::optional<FieldChunkDiskPersistence>
    create(std::string saveRoot, std::string layer, int bytesPerCell);

    static IRMath::ivec2 regionOf(IRMath::ivec2 chunkCoord);
    static IRMath::ivec2 regionFirstChunk(IRMath::ivec2 region);
    /// The mask bit of @p chunkCoord inside its region.
    static int regionLocalBit(IRMath::ivec2 chunkCoord);

    std::string regionPath(IRMath::ivec2 region) const;

    /// `nullopt` quietly for a missing file; with a warning for a malformed
    /// one (bad magic or version, truncation, a header naming another region
    /// or schema, a `CELL` size that disagrees with the mask).
    std::optional<FieldRegion> loadRegion(IRMath::ivec2 region) const;

    /// Writes a sibling temporary file and renames it over the target. An
    /// empty mask removes the region file instead.
    bool saveRegion(IRMath::ivec2 region, const FieldRegion &data) const;

    bool regionExists(IRMath::ivec2 region) const;

    /// Deletes this layer's region files and returns the count. Touches only
    /// grammar-matched regular files two numeric bucket levels under the layer
    /// directory, never follows a symlink, and removes only bucket directories
    /// it emptied. A symlinked layer directory is refused.
    int removeAll() const;

    const std::string &saveRoot() const {
        return m_saveRoot;
    }

    const std::string &layer() const {
        return m_layer;
    }

    int bytesPerCell() const {
        return m_bytesPerCell;
    }

    std::size_t regionCellBytes(int chunkCount) const {
        return static_cast<std::size_t>(chunkCount) *
               static_cast<std::size_t>(IRPrefab::Spatial::kFieldChunkCells) *
               static_cast<std::size_t>(m_bytesPerCell);
    }

  private:
    FieldChunkDiskPersistence(std::string saveRoot, std::string layer, int bytesPerCell);

    std::string m_saveRoot;
    std::string m_layer;
    std::string m_layerDir;
    int m_bytesPerCell = 1;
};

} // namespace IRWorld

#endif /* FIELD_CHUNK_PERSISTENCE_H */

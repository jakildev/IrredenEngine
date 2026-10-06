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
//
// A layer may opt into auxiliary per-cell payloads beside `CELL`: each is a
// tagged chunk holding a 32-byte region-local chunk mask (a subset of `CMSK`)
// followed by the selected chunks' cells in ascending mask-bit order. The
// transport validates masks and sizes and carries the bytes; the layer's
// owner gives them meaning. A layer with auxiliary schemas writes container
// version 2; both versions load.

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
/// The container version a layer without auxiliary payloads writes, and the
/// one every auxiliary-carrying layer writes.
constexpr std::uint32_t kFieldRegionVersionBase = 1;
constexpr std::uint32_t kFieldRegionVersionAuxiliary = 2;

/// Bit `i = ly * 16 + lx` of a region-local chunk mask (byte `i / 8`, bit
/// `i % 8`) names region-local field chunk `(lx, ly)`.
struct FieldRegionChunkMask {
    std::array<std::uint8_t, kFieldRegionMaskBytes> mask_{};

    bool hasChunk(int bit) const {
        return ((mask_[static_cast<std::size_t>(bit / 8)] >> (bit % 8)) & 1u) != 0;
    }

    void setChunk(int bit) {
        mask_[static_cast<std::size_t>(bit / 8)] |= static_cast<std::uint8_t>(1u << (bit % 8));
    }

    int chunkCount() const;

    /// Every chunk this mask names is in @p other.
    bool isSubsetOf(const FieldRegionChunkMask &other) const;
};

/// One opted-in auxiliary payload: the layer's tag and the bytes each cell
/// takes in it.
struct FieldRegionAuxSchema {
    std::array<char, 4> tag_{};
    int bytesPerCell_ = 0;
};

/// One region's auxiliary payload: its chunk mask (a subset of the region's)
/// and the selected chunks' cells packed like `FieldRegion::cells_`.
struct FieldRegionAux : FieldRegionChunkMask {
    std::vector<std::uint8_t> cells_;
};

/// One region's present field chunks; `cells_` packs the present field chunks
/// in ascending bit order, each `kFieldChunkCells * bytesPerCell` bytes,
/// row-major. `aux_` holds one entry per schema the layer declared, in
/// declaration order; an omitted payload reads back with an empty mask.
/// `version_` is the container version a loaded region came from.
struct FieldRegion : FieldRegionChunkMask {
    std::vector<std::uint8_t> cells_;
    std::vector<FieldRegionAux> aux_;
    std::uint32_t version_ = kFieldRegionVersionAuxiliary;
};

class FieldChunkDiskPersistence {
  public:
    /// `nullopt` for an empty @p saveRoot, a @p layer outside
    /// `[a-z][a-z0-9_]{0,31}`, @p bytesPerCell outside `[1, 255]`, or an
    /// @p auxiliary schema with a repeated or reserved tag (`FHDR`, `CMSK`,
    /// `CELL`) or a per-cell size outside `[1, 255]`.
    static std::optional<FieldChunkDiskPersistence> create(
        std::string saveRoot,
        std::string layer,
        int bytesPerCell,
        std::vector<FieldRegionAuxSchema> auxiliary = {}
    );

    static IRMath::ivec2 regionOf(IRMath::ivec2 chunkCoord);
    static IRMath::ivec2 regionFirstChunk(IRMath::ivec2 region);
    /// The mask bit of @p chunkCoord inside its region.
    static int regionLocalBit(IRMath::ivec2 chunkCoord);

    std::string regionPath(IRMath::ivec2 region) const;

    /// `nullopt` quietly for a missing file; with a warning for a malformed
    /// one (bad magic or version, truncation, a header naming another region
    /// or schema, a `CELL` size that disagrees with the mask, an auxiliary
    /// payload whose mask leaves the region mask or whose size disagrees with
    /// its own mask). Nothing of a malformed region is returned.
    std::optional<FieldRegion> loadRegion(IRMath::ivec2 region) const;

    /// Writes a sibling temporary file and renames it over the target. An
    /// empty mask removes the region file instead. `data.aux_` carries one
    /// entry per declared schema (fewer entries are omitted payloads); an
    /// entry with an empty mask writes no chunk.
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

    const std::vector<FieldRegionAuxSchema> &auxiliary() const {
        return m_auxiliary;
    }

    /// The container version this layer writes.
    std::uint32_t version() const {
        return m_auxiliary.empty() ? kFieldRegionVersionBase : kFieldRegionVersionAuxiliary;
    }

    std::size_t regionCellBytes(int chunkCount) const {
        return cellBytes(chunkCount, m_bytesPerCell);
    }

    static std::size_t cellBytes(int chunkCount, int bytesPerCell) {
        return static_cast<std::size_t>(chunkCount) *
               static_cast<std::size_t>(IRPrefab::Spatial::kFieldChunkCells) *
               static_cast<std::size_t>(bytesPerCell);
    }

  private:
    FieldChunkDiskPersistence(
        std::string saveRoot,
        std::string layer,
        int bytesPerCell,
        std::vector<FieldRegionAuxSchema> auxiliary
    );

    std::string m_saveRoot;
    std::string m_layer;
    std::string m_layerDir;
    int m_bytesPerCell = 1;
    std::vector<FieldRegionAuxSchema> m_auxiliary;
};

} // namespace IRWorld

#endif /* FIELD_CHUNK_PERSISTENCE_H */

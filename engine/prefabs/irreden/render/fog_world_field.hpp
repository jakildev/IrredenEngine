#ifndef FOG_WORLD_FIELD_H
#define FOG_WORLD_FIELD_H

// The CPU authority for fog-of-war state: one cell per integer world column,
// unbounded, stored in 32×32 field chunks. GPU-free, so tests construct it
// without a render device. Contract: docs/design/fog-of-war-world-field.md
// (D1–D5, D11).
//
// With persistence set, every region (16×16 field chunks) is resident or not.
// The first read, write or gather expansion that touches a non-resident region
// probes its file once and loads what it holds before anything else happens,
// so a write never shadows a disk copy. A changed write marks the region
// persistence-dirty; a load does not. CPU access sets the region's access bit,
// which `evict` reads.
//
// Beside the persistent cells sits the transient vision-tier layer (D8):
// discs stamped by vision sources past the analytic cap, cleared with the
// vision set. It never persists, probes, evicts or sets an access bit, and
// every read (`getCell`, `peekCell`, the gather) takes the per-cell maximum of
// both layers.

#include <irreden/ir_math.hpp>
#include <irreden/spatial/chunked_field.hpp>
#include <irreden/world/field_chunk_persistence.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IRComponents {

constexpr std::uint8_t kFogStateUnexplored = 0;
constexpr std::uint8_t kFogStateExplored = 128;
constexpr std::uint8_t kFogStateVisible = 255;

} // namespace IRComponents

namespace IRPrefab::Fog {

constexpr int kFogRevealRadiusMax = 1024;
constexpr int kFogFieldBytesPerCell = 1;
constexpr const char *kFogFieldLayer = "fog";

/// The camera depth slab the GPU window is exact for: matter at
/// `z ∈ [-kFogWindowDepthHalfBand, kFogWindowDepthHalfBand)` that lands
/// anywhere on the canvas has its column inside the window.
constexpr int kFogWindowDepthHalfBand = 128;
/// Window edges are multiples of this (two field chunks) and never exceed
/// `kFogWindowEdgeMax` (16 MiB of RGBA8); a capped window over-fogs its
/// periphery.
constexpr int kFogWindowEdgeQuantum = 64;
constexpr int kFogWindowEdgeMax = 4096;
/// Cells the window adds past the covered radius: 32 for the chunk snap of
/// the origin and 1 for column rounding.
constexpr int kFogWindowSnapMargin = 33;
/// Field chunks the eviction keep rectangle extends past the window.
constexpr int kFogResidentMarginChunks = 4;

/// Resident counts now; probes, loads, saves and evictions since the
/// previous `WorldField::stats()` call.
struct WorldFieldStats {
    int residentRegions_ = 0;
    int residentChunks_ = 0;
    int probes_ = 0;
    int loads_ = 0;
    int saves_ = 0;
    int evictions_ = 0;
};

class WorldField {
  public:
    using Cells = IRPrefab::Spatial::ChunkedField2D<std::uint8_t>;

    /// False once any field chunk is present: a late root would shadow disk
    /// state, and the field never merges.
    bool setPersistence(IRWorld::FieldChunkDiskPersistence persistence) {
        if (m_cells.chunkCount() != 0) {
            return false;
        }
        m_persistence.emplace(std::move(persistence));
        m_regions.clear();
        return true;
    }

    bool hasPersistence() const {
        return m_persistence.has_value();
    }

    /// `max(persistent, transient)`; absent cells read `kFogStateUnexplored`.
    /// Not const: the read can load the cell's region.
    std::uint8_t getCell(IRMath::ivec2 cell) {
        touchRegion(regionOfCell(cell), true);
        std::uint8_t state = IRComponents::kFogStateUnexplored;
        std::uint8_t transient = IRComponents::kFogStateUnexplored;
        m_cells.getCell(cell, state);
        m_transient.getCell(cell, transient);
        return IRMath::max(state, transient);
    }

    /// `max(persistent, transient)` over the layers whose field chunk holding
    /// @p cell is in memory, or nullopt when neither is. Never probes and
    /// never sets the access bit, so a fixture can observe residency without
    /// changing what the next eviction drops.
    std::optional<std::uint8_t> peekCell(IRMath::ivec2 cell) const {
        std::uint8_t state = IRComponents::kFogStateUnexplored;
        std::uint8_t transient = IRComponents::kFogStateUnexplored;
        const bool persistentPresent = m_cells.getCell(cell, state);
        const bool transientPresent = m_transient.getCell(cell, transient);
        if (!persistentPresent && !transientPresent) {
            return std::nullopt;
        }
        return IRMath::max(state, transient);
    }

    /// Writes the persistent layer only: under a transient disc the cell
    /// still reads visible.
    bool setCell(IRMath::ivec2 cell, std::uint8_t state) {
        RegionRecord *record = touchRegion(regionOfCell(cell), true);
        if (state == IRComponents::kFogStateUnexplored &&
            m_cells.findChunk(IRPrefab::Spatial::fieldChunkOf(cell)) == nullptr) {
            return false;
        }
        if (!m_cells.setCell(cell, state)) {
            return false;
        }
        markPersistenceDirty(record);
        return true;
    }

    /// Writes @p state to @p count cells along +x from @p firstCell and
    /// returns the changed-cell count. Requires `count >= 0` and the run
    /// representable in int32.
    int fillRow(IRMath::ivec2 firstCell, int count, std::uint8_t state) {
        if (!m_persistence.has_value()) {
            return m_cells.fillRow(firstCell, count, state);
        }
        int changed = 0;
        int x = firstCell.x;
        int remaining = count;
        while (remaining > 0) {
            const IRMath::ivec2 cell{x, firstCell.y};
            const int regionLocalX = cell.x & (kRegionEdgeCells - 1);
            const int run = IRMath::min(remaining, kRegionEdgeCells - regionLocalX);
            RegionRecord *record = touchRegion(regionOfCell(cell), true);
            const int runChanged = m_cells.fillRow(cell, run, state);
            if (runChanged > 0) {
                markPersistenceDirty(record);
            }
            changed += runChanged;
            remaining -= run;
            if (remaining > 0) {
                x += run;
            }
        }
        return changed;
    }

    /// Marks every cell whose centre lies within @p radius of @p centre
    /// (`dx² + dy² <= r²`) visible and returns the changed-cell count. The
    /// radius clamps to `kFogRevealRadiusMax`; the part of the disc beyond the
    /// int32 range is skipped. Cells outside the disc are never downgraded.
    int revealRadius(IRMath::ivec2 centre, int radius) {
        if (radius < 0) {
            return 0;
        }
        const std::int64_t r = IRMath::min(radius, kFogRevealRadiusMax);
        int changed = 0;
        forEachDiscRow(centre, r, r * r, [&](IRMath::ivec2 firstCell, int count) {
            changed += fillRow(firstCell, count, IRComponents::kFogStateVisible);
        });
        return changed;
    }

    /// Stamps the vision-tier disc of a source at world point @p centre into
    /// the transient layer: every cell whose centre lies within @p radius of
    /// `roundHalfUp(centre)` (`dx² + dy² <= radius²`, the `revealRadius`
    /// metric) reads visible until `clearTransient`. The radius clamps to
    /// `kFogRevealRadiusMax`; a non-positive one stamps nothing. Returns the
    /// changed-cell count.
    int stampTransientDisc(IRMath::vec2 centre, float radius) {
        if (!(radius > 0.0f)) {
            return 0;
        }
        const float clamped = IRMath::min(radius, static_cast<float>(kFogRevealRadiusMax));
        const IRMath::ivec2 cell{IRMath::roundHalfUp(centre.x), IRMath::roundHalfUp(centre.y)};
        const auto radiusSquared = static_cast<std::int64_t>(IRMath::floor(clamped * clamped));
        const auto rowRadius = static_cast<std::int64_t>(IRMath::floor(clamped));
        int changed = 0;
        forEachDiscRow(cell, rowRadius, radiusSquared, [&](IRMath::ivec2 firstCell, int count) {
            changed += m_transient.fillRow(firstCell, count, IRComponents::kFogStateVisible);
        });
        return changed;
    }

    /// Drops every transient disc. The persistent layer, the regions and the
    /// persistence handle are untouched.
    void clearTransient() {
        m_transient.clear();
    }

    /// Resets every persistent cell to unexplored; the transient layer is the
    /// vision set's and survives. With persistence it also deletes the layer's
    /// region files and forgets every region, so the next access probes again.
    void clear() {
        m_cells.clear();
        if (!m_persistence.has_value()) {
            return;
        }
        m_persistence->removeAll();
        m_regions.clear();
    }

    /// Saves every persistence-dirty resident region; returns the number
    /// saved. A region whose save fails stays dirty.
    int flush() {
        if (!m_persistence.has_value()) {
            return 0;
        }
        int saved = 0;
        for (auto &[key, record] : m_regions) {
            if (record.persistenceDirty_ &&
                saveRegion(IRPrefab::Spatial::unpackFieldChunkKey(key))) {
                record.persistenceDirty_ = false;
                ++saved;
            }
        }
        return saved;
    }

    /// Drops each resident region that does not intersect the field-chunk
    /// rectangle `[keepMinChunk, keepMaxChunk]` and whose access bit is clear,
    /// saving it first when dirty, then clears every access bit. Returns the
    /// number of regions evicted; without persistence it does nothing. A
    /// region touched since the previous call survives it.
    int evict(IRMath::ivec2 keepMinChunk, IRMath::ivec2 keepMaxChunk) {
        if (!m_persistence.has_value()) {
            return 0;
        }
        m_evictScratch.clear();
        for (auto &[key, record] : m_regions) {
            const IRMath::ivec2 region = IRPrefab::Spatial::unpackFieldChunkKey(key);
            const IRMath::ivec2 firstChunk =
                IRWorld::FieldChunkDiskPersistence::regionFirstChunk(region);
            const IRMath::ivec2 lastChunk = firstChunk + (IRWorld::kFieldRegionEdgeChunks - 1);
            const bool intersectsKeep =
                lastChunk.x >= keepMinChunk.x && firstChunk.x <= keepMaxChunk.x &&
                lastChunk.y >= keepMinChunk.y && firstChunk.y <= keepMaxChunk.y;
            if (!intersectsKeep && !record.accessed_) {
                m_evictScratch.push_back(key);
            }
            record.accessed_ = false;
        }

        int evicted = 0;
        for (IRPrefab::Spatial::FieldChunkKey key : m_evictScratch) {
            const IRMath::ivec2 region = IRPrefab::Spatial::unpackFieldChunkKey(key);
            RegionRecord &record = m_regions.at(key);
            if (record.persistenceDirty_ && !saveRegion(region)) {
                continue;
            }
            const IRMath::ivec2 firstChunk =
                IRWorld::FieldChunkDiskPersistence::regionFirstChunk(region);
            for (int ly = 0; ly < IRWorld::kFieldRegionEdgeChunks; ++ly) {
                for (int lx = 0; lx < IRWorld::kFieldRegionEdgeChunks; ++lx) {
                    m_cells.eraseChunk(firstChunk + IRMath::ivec2{lx, ly});
                }
            }
            m_regions.erase(key);
            ++evicted;
        }
        m_counters.evictions_ += evicted;
        return evicted;
    }

    /// The gather's view of one field chunk: makes its region resident
    /// without setting the access bit. Null for an absent field chunk.
    /// Invalidated by any later mutation, `clear` or `evict`.
    const Cells::FieldChunk *findChunkForGather(IRMath::ivec2 chunkCoord) {
        touchRegion(IRWorld::FieldChunkDiskPersistence::regionOf(chunkCoord), false);
        return m_cells.findChunk(chunkCoord);
    }

    /// The transient layer's field chunk, or null when absent. Invalidated by
    /// any later stamp or `clearTransient`.
    const Cells::FieldChunk *findTransientChunk(IRMath::ivec2 chunkCoord) const {
        return m_transient.findChunk(chunkCoord);
    }

    /// Replaces @p out with the field chunks either layer changed since the
    /// previous call (sorted, unique) and refreshes their summaries. The only
    /// drain of the pending set; an undrained field grows it.
    void consumePending(std::vector<IRPrefab::Spatial::FieldChunkKey> &out) {
        m_cells.dirtyKeys(out);
        m_cells.update();
        if (!m_transient.hasDirtyKeys()) {
            return;
        }
        m_transient.dirtyKeys(m_transientKeysScratch);
        m_transient.update();
        out.insert(out.end(), m_transientKeysScratch.begin(), m_transientKeysScratch.end());
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

    WorldFieldStats stats() {
        WorldFieldStats result = m_counters;
        result.residentRegions_ = static_cast<int>(m_regions.size());
        result.residentChunks_ = static_cast<int>(m_cells.chunkCount());
        m_counters = {};
        return result;
    }

  private:
    static constexpr int kRegionEdgeCells =
        IRWorld::kFieldRegionEdgeChunks * IRPrefab::Spatial::kFieldChunkEdge;

    struct RegionRecord {
        bool accessed_ = false;
        bool persistenceDirty_ = false;
    };

    Cells m_cells;
    Cells m_transient;
    std::vector<IRPrefab::Spatial::FieldChunkKey> m_transientKeysScratch;
    std::optional<IRWorld::FieldChunkDiskPersistence> m_persistence;
    std::unordered_map<IRPrefab::Spatial::FieldChunkKey, RegionRecord> m_regions;
    std::vector<IRPrefab::Spatial::FieldChunkKey> m_evictScratch;
    IRWorld::FieldRegion m_regionScratch;
    WorldFieldStats m_counters;

    static IRMath::ivec2 regionOfCell(IRMath::ivec2 cell) {
        return IRWorld::FieldChunkDiskPersistence::regionOf(IRPrefab::Spatial::fieldChunkOf(cell));
    }

    /// Calls @p row(firstCell, count) for each row of the disc of cells within
    /// `dy <= rowRadius` and `dx² + dy² <= radiusSquared` of @p centre, with the
    /// row bounds computed in 64 bits and the part beyond int32 skipped.
    template <typename RowFn>
    static void forEachDiscRow(
        IRMath::ivec2 centre, std::int64_t rowRadius, std::int64_t radiusSquared, RowFn &&row
    ) {
        constexpr std::int64_t kCellMin = std::numeric_limits<std::int32_t>::min();
        constexpr std::int64_t kCellMax = std::numeric_limits<std::int32_t>::max();
        for (std::int64_t dy = -rowRadius; dy <= rowRadius; ++dy) {
            const std::int64_t y = centre.y + dy;
            if (y < kCellMin || y > kCellMax) {
                continue;
            }
            const std::int64_t halfWidth = IRMath::isqrt(radiusSquared - dy * dy);
            const std::int64_t first = IRMath::max(centre.x - halfWidth, kCellMin);
            const std::int64_t last = IRMath::min(centre.x + halfWidth, kCellMax);
            if (first > last) {
                continue;
            }
            row(IRMath::ivec2{static_cast<int>(first), static_cast<int>(y)},
                static_cast<int>(last - first + 1));
        }
    }

    static void markPersistenceDirty(RegionRecord *record) {
        if (record != nullptr) {
            record->persistenceDirty_ = true;
        }
    }

    RegionRecord *touchRegion(IRMath::ivec2 region, bool cpuAccess) {
        if (!m_persistence.has_value()) {
            return nullptr;
        }
        const IRPrefab::Spatial::FieldChunkKey key = IRPrefab::Spatial::packFieldChunkKey(region);
        auto [it, inserted] = m_regions.try_emplace(key);
        RegionRecord &record = it->second;
        record.accessed_ = record.accessed_ || cpuAccess;
        if (inserted) {
            loadRegion(region);
        }
        return &record;
    }

    void loadRegion(IRMath::ivec2 region) {
        ++m_counters.probes_;
        std::optional<IRWorld::FieldRegion> loaded = m_persistence->loadRegion(region);
        if (!loaded.has_value()) {
            return;
        }
        ++m_counters.loads_;
        const IRMath::ivec2 firstChunk =
            IRWorld::FieldChunkDiskPersistence::regionFirstChunk(region);
        std::size_t offset = 0;
        for (int bit = 0; bit < IRWorld::kFieldRegionChunks; ++bit) {
            if (!loaded->hasChunk(bit)) {
                continue;
            }
            const IRMath::ivec2 local{
                bit % IRWorld::kFieldRegionEdgeChunks,
                bit / IRWorld::kFieldRegionEdgeChunks
            };
            m_cells.assignChunk(
                firstChunk + local,
                std::span<const std::uint8_t, IRPrefab::Spatial::kFieldChunkCells>{
                    loaded->cells_.data() + offset,
                    IRPrefab::Spatial::kFieldChunkCells
                }
            );
            offset += IRPrefab::Spatial::kFieldChunkCells;
        }
    }

    bool saveRegion(IRMath::ivec2 region) {
        IRWorld::FieldRegion &data = m_regionScratch;
        data.mask_.fill(0);
        data.cells_.clear();
        const IRMath::ivec2 firstChunk =
            IRWorld::FieldChunkDiskPersistence::regionFirstChunk(region);
        for (int bit = 0; bit < IRWorld::kFieldRegionChunks; ++bit) {
            const IRMath::ivec2 local{
                bit % IRWorld::kFieldRegionEdgeChunks,
                bit / IRWorld::kFieldRegionEdgeChunks
            };
            const Cells::FieldChunk *fieldChunk = m_cells.findChunk(firstChunk + local);
            if (fieldChunk == nullptr) {
                continue;
            }
            data.setChunk(bit);
            data.cells_
                .insert(data.cells_.end(), fieldChunk->cells().begin(), fieldChunk->cells().end());
        }
        if (!m_persistence->saveRegion(region, data)) {
            return false;
        }
        ++m_counters.saves_;
        return true;
    }
};

namespace detail {

/// The window edge the umbrella formula gives @p canvasSize before the cap:
/// the smallest multiple of `kFogWindowEdgeQuantum` that is at least
/// `2 × (R + √2 × kFogWindowDepthHalfBand + kFogWindowSnapMargin)`, where
/// `R` is the world-XY radius of the canvas's projected footprint at z = 0.
/// A voxel at height `z` that draws anywhere on the canvas has its column
/// within `R + √2 × |z|` of the centre, and rotation preserves that length.
inline int windowEdgeUncapped(IRMath::ivec2 canvasSize) {
    const float footprintRadius = IRMath::length(IRMath::vec2(canvasSize) * 0.5f) / IRMath::kSqrt2;
    const float columnRadius = footprintRadius + IRMath::kSqrt2 * kFogWindowDepthHalfBand;
    const float required = 2.0f * (columnRadius + static_cast<float>(kFogWindowSnapMargin));
    const int quanta = static_cast<int>(IRMath::ceil(required / kFogWindowEdgeQuantum));
    return quanta * kFogWindowEdgeQuantum;
}

/// The RGBA8 window edge for a fog canvas of @p canvasSize trixels:
/// `windowEdgeUncapped` capped at `kFogWindowEdgeMax`.
inline int windowEdgeForCanvas(IRMath::ivec2 canvasSize) {
    return IRMath::min(windowEdgeUncapped(canvasSize), kFogWindowEdgeMax);
}

/// The Chebyshev radius of columns a window of @p edge covers exactly.
constexpr int windowCoveredRadius(int edge) {
    return edge / 2 - kFogWindowSnapMargin;
}

/// The window origin for the world point @p centre under the viewport centre:
/// the rounded centre snapped down to a field-chunk boundary, less half the
/// edge (a multiple of the field-chunk edge, so the origin stays aligned).
inline IRMath::ivec2 windowOriginForCentre(IRMath::vec2 centre, int edge) {
    const IRMath::ivec2 rounded{IRMath::roundHalfUp(centre.x), IRMath::roundHalfUp(centre.y)};
    return IRPrefab::Spatial::fieldChunkOf(rounded) * IRPrefab::Spatial::kFieldChunkEdge - edge / 2;
}

/// The texel world column @p column lives at, whatever the origin:
/// `floorMod(column, edge)`. The origin decides only which columns are in
/// the window.
inline IRMath::ivec2 windowTexel(IRMath::ivec2 column, int edge) {
    return {
        static_cast<int>(IRMath::floorMod(column.x, edge)),
        static_cast<int>(IRMath::floorMod(column.y, edge))
    };
}

/// The in-window field chunk shown at texture chunk @p textureChunk for a
/// window whose first chunk is @p originChunk: the inverse of
/// `floorMod(chunk, edgeChunks)` restricted to the window.
inline IRMath::ivec2
windowChunkOfTextureChunk(IRMath::ivec2 originChunk, IRMath::ivec2 textureChunk, int edgeChunks) {
    return originChunk +
           IRMath::ivec2{
               static_cast<int>(IRMath::floorMod(textureChunk.x - originChunk.x, edgeChunks)),
               static_cast<int>(IRMath::floorMod(textureChunk.y - originChunk.y, edgeChunks))
           };
}

/// One texture-space upload: `texel_` is the top-left texel, `size_` the
/// extent. Field-chunk aligned and inside the texture (never across its
/// wrap).
struct WindowUploadRect {
    IRMath::ivec2 texel_{0};
    IRMath::ivec2 size_{0};
};

/// The field chunks a gather re-expands and the texture rectangles it
/// uploads.
struct WindowGatherPlan {
    std::vector<IRMath::ivec2> chunks_;
    std::vector<WindowUploadRect> rects_;
};

/// Appends the rectangles covering texture chunks `[first, first + count)`
/// along one axis, the full edge along the other, split once where the
/// range wraps past the texture edge.
inline void appendWrappedStrip(
    int firstTextureChunk, int count, int edge, bool alongX, std::vector<WindowUploadRect> &rects
) {
    using IRPrefab::Spatial::kFieldChunkEdge;
    const int edgeChunks = edge / kFieldChunkEdge;
    int first = firstTextureChunk;
    int remaining = count;
    while (remaining > 0) {
        const int run = IRMath::min(remaining, edgeChunks - first);
        if (alongX) {
            rects.push_back(
                {IRMath::ivec2{first * kFieldChunkEdge, 0},
                 IRMath::ivec2{run * kFieldChunkEdge, edge}}
            );
        } else {
            rects.push_back(
                {IRMath::ivec2{0, first * kFieldChunkEdge},
                 IRMath::ivec2{edge, run * kFieldChunkEdge}}
            );
        }
        remaining -= run;
        first = 0;
    }
}

/// Plans the gather of the @p edge-square, toroidally addressed window whose
/// first column is @p origin (a multiple of the field-chunk edge; column `c`
/// is at texel `floorMod(c, edge)`). An unset @p previousOrigin, or a move of
/// at least the window's width of field chunks on an axis, plans the whole
/// window in one-field-chunk-row strips. A smaller move plans the newly
/// exposed strip on each moved axis, split at the texture wrap into at most
/// two rectangles. Pending field chunks inside the window and outside those
/// strips are planned in place, one rectangle per run of adjacent chunks in a
/// row (split at the wrap); pending chunks outside the window are dropped.
/// @p pendingKeys must be sorted and unique, as `consumePending` returns them.
inline void planWindowGather(
    std::optional<IRMath::ivec2> previousOrigin,
    IRMath::ivec2 origin,
    int edge,
    std::span<const IRPrefab::Spatial::FieldChunkKey> pendingKeys,
    WindowGatherPlan &out
) {
    using IRPrefab::Spatial::kFieldChunkEdge;
    out.chunks_.clear();
    out.rects_.clear();
    const IRMath::ivec2 originChunk = IRPrefab::Spatial::fieldChunkOf(origin);
    const int edgeChunks = edge / kFieldChunkEdge;

    IRMath::ivec2 moved{0};
    bool whole = !previousOrigin.has_value();
    if (!whole) {
        moved = originChunk - IRPrefab::Spatial::fieldChunkOf(*previousOrigin);
        whole = IRMath::abs(moved.x) >= edgeChunks || IRMath::abs(moved.y) >= edgeChunks;
    }
    if (whole) {
        for (int row = 0; row < edgeChunks; ++row) {
            for (int column = 0; column < edgeChunks; ++column) {
                out.chunks_.push_back(
                    windowChunkOfTextureChunk(originChunk, {column, row}, edgeChunks)
                );
            }
            out.rects_.push_back(
                {IRMath::ivec2{0, row * kFieldChunkEdge}, IRMath::ivec2{edge, kFieldChunkEdge}}
            );
        }
        return;
    }

    // The newly exposed local chunk range per axis: the trailing `moved`
    // columns or rows of the new window for a positive move, the leading
    // ones for a negative move.
    const auto exposedRange = [edgeChunks](int delta, int &first, int &last) {
        first = delta > 0 ? edgeChunks - delta : 0;
        last = delta < 0 ? -delta : (delta > 0 ? edgeChunks : 0);
    };
    int exposedX0 = 0;
    int exposedX1 = 0;
    int exposedY0 = 0;
    int exposedY1 = 0;
    exposedRange(moved.x, exposedX0, exposedX1);
    exposedRange(moved.y, exposedY0, exposedY1);
    const auto inExposedStrip = [&](IRMath::ivec2 local) {
        return (local.x >= exposedX0 && local.x < exposedX1) ||
               (local.y >= exposedY0 && local.y < exposedY1);
    };
    for (int localY = 0; localY < edgeChunks; ++localY) {
        for (int localX = 0; localX < edgeChunks; ++localX) {
            if (inExposedStrip({localX, localY})) {
                out.chunks_.push_back(originChunk + IRMath::ivec2{localX, localY});
            }
        }
    }
    if (moved.x != 0) {
        appendWrappedStrip(
            static_cast<int>(IRMath::floorMod(originChunk.x + exposedX0, edgeChunks)),
            exposedX1 - exposedX0,
            edge,
            true,
            out.rects_
        );
    }
    if (moved.y != 0) {
        appendWrappedStrip(
            static_cast<int>(IRMath::floorMod(originChunk.y + exposedY0, edgeChunks)),
            exposedY1 - exposedY0,
            edge,
            false,
            out.rects_
        );
    }

    const std::size_t pendingStart = out.chunks_.size();
    for (IRPrefab::Spatial::FieldChunkKey key : pendingKeys) {
        const IRMath::ivec2 local = IRPrefab::Spatial::unpackFieldChunkKey(key) - originChunk;
        if (local.x >= 0 && local.x < edgeChunks && local.y >= 0 && local.y < edgeChunks &&
            !inExposedStrip(local)) {
            out.chunks_.push_back(originChunk + local);
        }
    }
    std::sort(
        out.chunks_.begin() + static_cast<std::ptrdiff_t>(pendingStart),
        out.chunks_.end(),
        [](IRMath::ivec2 a, IRMath::ivec2 b) { return a.y != b.y ? a.y < b.y : a.x < b.x; }
    );

    std::size_t runStart = pendingStart;
    for (std::size_t i = pendingStart + 1; i <= out.chunks_.size(); ++i) {
        const bool continues = i < out.chunks_.size() && out.chunks_[i].y == out.chunks_[i - 1].y &&
                               out.chunks_[i].x == out.chunks_[i - 1].x + 1;
        if (continues) {
            continue;
        }
        if (runStart < out.chunks_.size()) {
            const IRMath::ivec2 firstChunk = out.chunks_[runStart];
            const int textureRow = static_cast<int>(IRMath::floorMod(firstChunk.y, edgeChunks));
            int textureColumn = static_cast<int>(IRMath::floorMod(firstChunk.x, edgeChunks));
            int remaining = static_cast<int>(i - runStart);
            while (remaining > 0) {
                const int run = IRMath::min(remaining, edgeChunks - textureColumn);
                out.rects_.push_back(
                    {IRMath::ivec2{textureColumn * kFieldChunkEdge, textureRow * kFieldChunkEdge},
                     IRMath::ivec2{run * kFieldChunkEdge, kFieldChunkEdge}}
                );
                remaining -= run;
                textureColumn = 0;
            }
        }
        runStart = i;
    }
}

/// Writes @p rect's cells into @p scratch as RGBA8 rows of `rect.size_.x`
/// texels (`max(persistent, transient)` in .r, zero elsewhere), making each
/// covered region resident first. Each texture chunk shows the in-window
/// field chunk at its toroidal address for the window at @p origin.
/// @p scratch holds at least `rect.size_.x * rect.size_.y * 4` bytes.
inline void expandWindowChunks(
    WorldField &field,
    IRMath::ivec2 origin,
    int edge,
    const WindowUploadRect &rect,
    std::span<std::uint8_t> scratch
) {
    using IRPrefab::Spatial::kFieldChunkEdge;
    const IRMath::ivec2 originChunk = IRPrefab::Spatial::fieldChunkOf(origin);
    const int edgeChunks = edge / kFieldChunkEdge;
    const IRMath::ivec2 firstTextureChunk = rect.texel_ / kFieldChunkEdge;
    const IRMath::ivec2 chunkExtent = rect.size_ / kFieldChunkEdge;
    const std::size_t rowBytes = static_cast<std::size_t>(rect.size_.x) * 4;
    for (int chunkRow = 0; chunkRow < chunkExtent.y; ++chunkRow) {
        for (int chunkColumn = 0; chunkColumn < chunkExtent.x; ++chunkColumn) {
            const IRMath::ivec2 chunkCoord = windowChunkOfTextureChunk(
                originChunk,
                firstTextureChunk + IRMath::ivec2{chunkColumn, chunkRow},
                edgeChunks
            );
            const WorldField::Cells::FieldChunk *fieldChunk = field.findChunkForGather(chunkCoord);
            const WorldField::Cells::FieldChunk *transientChunk =
                field.findTransientChunk(chunkCoord);
            for (int y = 0; y < kFieldChunkEdge; ++y) {
                std::uint8_t *texel =
                    scratch.data() +
                    static_cast<std::size_t>(chunkRow * kFieldChunkEdge + y) * rowBytes +
                    static_cast<std::size_t>(chunkColumn * kFieldChunkEdge) * 4;
                const std::uint8_t *cells = fieldChunk == nullptr
                                                ? nullptr
                                                : fieldChunk->cells().data() + y * kFieldChunkEdge;
                const std::uint8_t *transient =
                    transientChunk == nullptr
                        ? nullptr
                        : transientChunk->cells().data() + y * kFieldChunkEdge;
                for (int x = 0; x < kFieldChunkEdge; ++x) {
                    std::uint8_t state =
                        cells == nullptr ? IRComponents::kFogStateUnexplored : cells[x];
                    if (transient != nullptr) {
                        state = IRMath::max(state, transient[x]);
                    }
                    texel[x * 4] = state;
                    texel[x * 4 + 1] = 0;
                    texel[x * 4 + 2] = 0;
                    texel[x * 4 + 3] = 0;
                }
            }
        }
    }
}

/// The gather's reusable buffers, held by the owning system so every frame is
/// allocation-free once the high-water marks are reached.
struct WindowGatherScratch {
    std::vector<IRPrefab::Spatial::FieldChunkKey> pendingKeys_;
    WindowGatherPlan plan_;
    std::vector<std::uint8_t> upload_;
};

/// One frame of the window gather, GPU-free: drains the field's pending set,
/// plans the window at @p origin against @p windowOrigin (the origin the
/// texture currently shows, updated here), expands each planned rectangle and
/// hands it to @p upload as `(rect, rgba8Rows)`, then, on a frame whose
/// origin changed, evicts every region outside the window's field-chunk
/// rectangle grown by `kFogResidentMarginChunks`. The eviction runs after the
/// expansion so an in-window region is probed once per residency epoch, and
/// on the first frame too, which clears the access bits the initial reveals
/// set. A second call in one frame finds nothing pending and issues nothing.
template <typename UploadFn>
inline void gatherWindow(
    WorldField &field,
    std::optional<IRMath::ivec2> &windowOrigin,
    IRMath::ivec2 origin,
    int edge,
    WindowGatherScratch &scratch,
    UploadFn &&upload
) {
    const bool originChanged = !windowOrigin.has_value() || *windowOrigin != origin;
    field.consumePending(scratch.pendingKeys_);
    planWindowGather(windowOrigin, origin, edge, scratch.pendingKeys_, scratch.plan_);
    windowOrigin = origin;
    for (const WindowUploadRect &rect : scratch.plan_.rects_) {
        const std::size_t bytes =
            static_cast<std::size_t>(rect.size_.x) * static_cast<std::size_t>(rect.size_.y) * 4;
        if (scratch.upload_.size() < bytes) {
            scratch.upload_.resize(bytes);
        }
        expandWindowChunks(field, origin, edge, rect, scratch.upload_);
        upload(rect, std::span<const std::uint8_t>{scratch.upload_.data(), bytes});
    }
    if (originChanged) {
        const IRMath::ivec2 originChunk = IRPrefab::Spatial::fieldChunkOf(origin);
        const int edgeChunks = edge / IRPrefab::Spatial::kFieldChunkEdge;
        field.evict(
            originChunk - kFogResidentMarginChunks,
            originChunk + (edgeChunks - 1 + kFogResidentMarginChunks)
        );
    }
}

} // namespace detail

} // namespace IRPrefab::Fog

#endif /* FOG_WORLD_FIELD_H */

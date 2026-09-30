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

#include <irreden/ir_math.hpp>
#include <irreden/spatial/chunked_field.hpp>
#include <irreden/world/field_chunk_persistence.hpp>

#include <algorithm>
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

    /// Absent cells read `kFogStateUnexplored`. Not const: the read can load
    /// the cell's region.
    std::uint8_t getCell(IRMath::ivec2 cell) {
        touchRegion(regionOfCell(cell), true);
        std::uint8_t state = IRComponents::kFogStateUnexplored;
        m_cells.getCell(cell, state);
        return state;
    }

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
        constexpr std::int64_t kCellMin = std::numeric_limits<std::int32_t>::min();
        constexpr std::int64_t kCellMax = std::numeric_limits<std::int32_t>::max();
        const std::int64_t r = IRMath::min(radius, kFogRevealRadiusMax);
        const std::int64_t radiusSquared = r * r;
        int changed = 0;
        for (std::int64_t dy = -r; dy <= r; ++dy) {
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
            changed += fillRow(
                IRMath::ivec2{static_cast<int>(first), static_cast<int>(y)},
                static_cast<int>(last - first + 1),
                IRComponents::kFogStateVisible
            );
        }
        return changed;
    }

    /// Resets every cell to unexplored. With persistence it also deletes the
    /// layer's region files and forgets every region, so the next access
    /// probes again.
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

    /// Replaces @p out with the field chunks changed since the previous call
    /// (sorted) and refreshes their summaries. The only drain of the pending
    /// set; an undrained field grows it.
    void consumePending(std::vector<IRPrefab::Spatial::FieldChunkKey> &out) {
        m_cells.dirtyKeys(out);
        m_cells.update();
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
    std::optional<IRWorld::FieldChunkDiskPersistence> m_persistence;
    std::unordered_map<IRPrefab::Spatial::FieldChunkKey, RegionRecord> m_regions;
    std::vector<IRPrefab::Spatial::FieldChunkKey> m_evictScratch;
    IRWorld::FieldRegion m_regionScratch;
    WorldFieldStats m_counters;

    static IRMath::ivec2 regionOfCell(IRMath::ivec2 cell) {
        return IRWorld::FieldChunkDiskPersistence::regionOf(IRPrefab::Spatial::fieldChunkOf(cell));
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

/// One texture-space upload: `texel_` is the top-left texel, `size_` the
/// extent. Field-chunk aligned and inside the window.
struct WindowUploadRect {
    IRMath::ivec2 texel_{0};
    IRMath::ivec2 size_{0};
};

struct WindowGatherPlan {
    std::vector<IRMath::ivec2> chunks_;
    std::vector<WindowUploadRect> rects_;
};

/// Plans the gather of the @p edge-square window at field column @p origin
/// (both multiples of the field-chunk edge; texel = column − origin). An unset
/// or different @p previousOrigin plans the whole window in one-field-chunk
/// strips; otherwise the pending field chunks inside the window, one
/// rectangle per run of adjacent pending field chunks in a field-chunk row.
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

    if (!previousOrigin.has_value() || *previousOrigin != origin) {
        for (int row = 0; row < edgeChunks; ++row) {
            for (int column = 0; column < edgeChunks; ++column) {
                out.chunks_.push_back(originChunk + IRMath::ivec2{column, row});
            }
            out.rects_.push_back(
                {IRMath::ivec2{0, row * kFieldChunkEdge}, IRMath::ivec2{edge, kFieldChunkEdge}}
            );
        }
        return;
    }

    for (IRPrefab::Spatial::FieldChunkKey key : pendingKeys) {
        const IRMath::ivec2 local = IRPrefab::Spatial::unpackFieldChunkKey(key) - originChunk;
        if (local.x >= 0 && local.x < edgeChunks && local.y >= 0 && local.y < edgeChunks) {
            out.chunks_.push_back(originChunk + local);
        }
    }
    std::sort(out.chunks_.begin(), out.chunks_.end(), [](IRMath::ivec2 a, IRMath::ivec2 b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });

    std::size_t runStart = 0;
    for (std::size_t i = 1; i <= out.chunks_.size(); ++i) {
        const bool continues = i < out.chunks_.size() && out.chunks_[i].y == out.chunks_[i - 1].y &&
                               out.chunks_[i].x == out.chunks_[i - 1].x + 1;
        if (continues) {
            continue;
        }
        if (runStart < out.chunks_.size()) {
            const IRMath::ivec2 firstLocal = out.chunks_[runStart] - originChunk;
            const int runLength = static_cast<int>(i - runStart);
            out.rects_.push_back(
                {firstLocal * kFieldChunkEdge,
                 IRMath::ivec2{runLength * kFieldChunkEdge, kFieldChunkEdge}}
            );
        }
        runStart = i;
    }
}

/// Writes @p rect's cells into @p scratch as RGBA8 rows of `rect.size_.x`
/// texels (state in .r, zero elsewhere), making each covered region resident
/// first. @p scratch holds at least `rect.size_.x * rect.size_.y * 4` bytes.
inline void expandWindowChunks(
    WorldField &field,
    IRMath::ivec2 origin,
    const WindowUploadRect &rect,
    std::span<std::uint8_t> scratch
) {
    using IRPrefab::Spatial::kFieldChunkEdge;
    const IRMath::ivec2 firstChunk = IRPrefab::Spatial::fieldChunkOf(origin + rect.texel_);
    const IRMath::ivec2 chunkExtent = rect.size_ / kFieldChunkEdge;
    const std::size_t rowBytes = static_cast<std::size_t>(rect.size_.x) * 4;
    for (int chunkRow = 0; chunkRow < chunkExtent.y; ++chunkRow) {
        for (int chunkColumn = 0; chunkColumn < chunkExtent.x; ++chunkColumn) {
            const WorldField::Cells::FieldChunk *fieldChunk =
                field.findChunkForGather(firstChunk + IRMath::ivec2{chunkColumn, chunkRow});
            for (int y = 0; y < kFieldChunkEdge; ++y) {
                std::uint8_t *texel =
                    scratch.data() +
                    static_cast<std::size_t>(chunkRow * kFieldChunkEdge + y) * rowBytes +
                    static_cast<std::size_t>(chunkColumn * kFieldChunkEdge) * 4;
                const std::uint8_t *cells = fieldChunk == nullptr
                                                ? nullptr
                                                : fieldChunk->cells().data() + y * kFieldChunkEdge;
                for (int x = 0; x < kFieldChunkEdge; ++x) {
                    texel[x * 4] = cells == nullptr ? IRComponents::kFogStateUnexplored : cells[x];
                    texel[x * 4 + 1] = 0;
                    texel[x * 4 + 2] = 0;
                    texel[x * 4 + 3] = 0;
                }
            }
        }
    }
}

} // namespace detail

} // namespace IRPrefab::Fog

#endif /* FOG_WORLD_FIELD_H */

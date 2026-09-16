#ifndef IR_FIELD_PLACEMENT_H
#define IR_FIELD_PLACEMENT_H

// PURPOSE: Composes the occupancy, clearance and region layers into one
//   PlacementField and answers "K valid cells near the anchor" with a seeded,
//   all-integer Poisson-disk draw that prunes at field-chunk granularity
//   before it reads a cell. Plain types and free functions, not a system.
//   See docs/design/chunked-field-placement-kit.md (D6, D7, D8).

#include <irreden/ir_math.hpp>
#include <irreden/spatial/chunked_field.hpp>
#include <irreden/spatial/field_clearance.hpp>
#include <irreden/spatial/field_regions.hpp>

#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IRPrefab::Spatial {

constexpr int kMaxPlacementHits = 65536;

// Bridson's per-sample attempt budget. Unrelated to PlacementParams::k_ (hits
// wanted): changing it moves both the hit list and where a sample is abandoned.
constexpr int kPlacementAttempts = 30;

struct PlacementHit {
    IRMath::ivec2 cell_;
    IRMath::ivec2 chunk_;

    bool operator==(const PlacementHit &) const = default;
};

// Counts distinct field chunks per query: `chunksConsidered_` is every chunk
// whose clearance summary the query consulted, `chunksPruned_` the subset
// that were present with a summary too low to hold a valid cell, and
// `candidatesDrawn_` the number of annulus attempts, accepted or rejected.
struct PlacementQueryStats {
    int chunksConsidered_ = 0;
    int chunksPruned_ = 0;
    int candidatesDrawn_ = 0;

    bool operator==(const PlacementQueryStats &) const = default;
};

struct PlacementParams {
    IRMath::ivec2 anchor_{};
    // Hits wanted, [1, kMaxPlacementHits]. The default is out of domain on
    // purpose: there is no meaningful default number of hits, and a rejected
    // query names the omission at the call site instead of returning an empty
    // vector that reads like a full field.
    int k_ = 0;
    int minSpacing_ = 1; // cells, [1, kMaxClearanceCells]; 1 is unconstrained
    int clearance_ = 0;  // cells, [0, field.maxClearance()]
    bool sameRegionAsAnchor_ = false;
    std::uint64_t seed_ = 0;
};

// Largest background-grid width `w` with `2 * w * w <= minSpacing^2`, so a grid
// cell of side `w` holds at most one sample. `minSpacing == 1` clamps to 1,
// where the grid cell is a single lattice cell and holds one sample by
// construction rather than by the diagonal bound.
inline int placementGridWidth(int minSpacing) {
    const std::int64_t width =
        IRMath::isqrt(static_cast<std::int64_t>(minSpacing) * minSpacing / 2);
    return static_cast<int>(width < 1 ? 1 : width);
}

// Occupancy is mutated only through this class so every change is
// acknowledged by update() together with the derived layers; a caller
// holding the occupancy field directly could acknowledge its dirty set
// without refreshing clearance and regions, which no precondition can
// then detect.
class PlacementField {
  public:
    explicit PlacementField(int maxClearance)
        : m_clearance(maxClearance) {}

    int maxClearance() const {
        return m_clearance.maxClearance();
    }

    void setCell(IRMath::ivec2 cell, std::uint8_t value) {
        m_occupancy.setCell(cell, value);
    }

    void clear() {
        m_occupancy.clear();
    }

    // True while a change since the last update() has not been folded into
    // the clearance and region layers; queryPlacements rejects such a field.
    bool hasPendingChanges() const {
        return m_occupancy.hasDirtyKeys();
    }

    // One dirty snapshot feeds clearance, then regions; only then is the
    // occupancy dirty set acknowledged, so neither layer sees a partial epoch.
    void update() {
        m_occupancy.dirtyKeys(m_dirtySnapshot);
        m_clearance.update(m_occupancy, m_dirtySnapshot);
        m_regions.update(m_occupancy, m_dirtySnapshot);
        m_occupancy.update();
    }

    const ChunkedField2D<std::uint8_t> &occupancy() const {
        return m_occupancy;
    }

    const FieldClearance &clearance() const {
        return m_clearance;
    }

    const FieldRegions &regions() const {
        return m_regions;
    }

  private:
    ChunkedField2D<std::uint8_t> m_occupancy;
    FieldClearance m_clearance;
    FieldRegions m_regions;
    std::vector<FieldChunkKey> m_dirtySnapshot;
};

namespace detail {

inline void validatePlacementParams(const PlacementField &field, const PlacementParams &params) {
    if (params.minSpacing_ < 1 || params.minSpacing_ > kMaxClearanceCells) {
        throw std::invalid_argument("placement minimum spacing is outside the supported domain");
    }
    if (params.clearance_ < 0 || params.clearance_ > field.maxClearance()) {
        throw std::invalid_argument("placement clearance is outside the field domain");
    }
    if (params.k_ < 1 || params.k_ > kMaxPlacementHits) {
        throw std::invalid_argument("placement hit count is outside the supported domain");
    }
    if (field.hasPendingChanges()) {
        throw std::invalid_argument("placement field has changes not yet folded in by update()");
    }
}

// One query's working state. Every RNG call site is one of three — the
// active-list index, and the two words of an annulus attempt — each consuming
// exactly one word, so the candidate sequence is a function of the seed alone.
class PlacementDraw {
  public:
    PlacementDraw(const PlacementField &field, const PlacementParams &params)
        : m_field(field)
        , m_params(params)
        , m_rng(params.seed_)
        , m_gridWidth(placementGridWidth(params.minSpacing_))
        , m_scanRadius(IRMath::divCeil(params.minSpacing_, m_gridWidth))
        , m_spacingSq(static_cast<std::int64_t>(params.minSpacing_) * params.minSpacing_)
        , m_clearanceSq(static_cast<std::int64_t>(params.clearance_) * params.clearance_) {}

    // The anchor is a sample whether or not it is a hit: it seeds the grid and
    // the active list unconditionally so an occupied anchor still grows a
    // frontier around itself.
    void run(std::vector<PlacementHit> &out) {
        const IRMath::ivec2 anchor = m_params.anchor_;
        m_active.push_back(anchor);
        gridInsert(anchor);
        if (isValid(anchor)) {
            out.push_back(hitFor(anchor));
        }

        while (!m_active.empty() && static_cast<int>(out.size()) < m_params.k_) {
            const std::uint32_t index =
                IRMath::uniformBelow(m_rng, static_cast<std::uint32_t>(m_active.size()));
            const IRMath::ivec2 sample = m_active[index];
            bool extended = false;
            for (int attempt = 0; attempt < kPlacementAttempts; ++attempt) {
                const IRMath::ivec2 offset = drawAnnulusOffset();
                ++m_stats.candidatesDrawn_;
                const std::int64_t candidateX = static_cast<std::int64_t>(sample.x) + offset.x;
                const std::int64_t candidateY = static_cast<std::int64_t>(sample.y) + offset.y;
                if (!std::in_range<std::int32_t>(candidateX) ||
                    !std::in_range<std::int32_t>(candidateY)) {
                    continue;
                }
                const IRMath::ivec2 candidate{
                    static_cast<std::int32_t>(candidateX),
                    static_cast<std::int32_t>(candidateY),
                };
                if (!spacingOk(candidate) || !isValid(candidate)) {
                    continue;
                }
                gridInsert(candidate);
                m_active.push_back(candidate);
                out.push_back(hitFor(candidate));
                extended = true;
                break;
            }
            if (!extended) {
                m_active[index] = m_active.back();
                m_active.pop_back();
            }
        }
    }

    const PlacementQueryStats &stats() const {
        return m_stats;
    }

  private:
    const PlacementField &m_field;
    const PlacementParams &m_params;
    IRMath::Pcg32 m_rng;
    const int m_gridWidth;
    const int m_scanRadius;
    const std::int64_t m_spacingSq;
    const std::int64_t m_clearanceSq;
    PlacementQueryStats m_stats;
    std::vector<IRMath::ivec2> m_active;
    std::unordered_map<std::uint64_t, IRMath::ivec2> m_grid;
    std::unordered_map<FieldChunkKey, bool> m_chunkVerdicts;

    // Two words per attempt, dx then dy; a rejected attempt keeps its two
    // words and the loop draws the next two.
    IRMath::ivec2 drawAnnulusOffset() {
        const int radius = m_params.minSpacing_;
        const auto span = static_cast<std::uint32_t>(4 * radius + 1);
        const std::int64_t radiusSq = static_cast<std::int64_t>(radius) * radius;
        for (;;) {
            const int dx = static_cast<int>(IRMath::uniformBelow(m_rng, span)) - 2 * radius;
            const int dy = static_cast<int>(IRMath::uniformBelow(m_rng, span)) - 2 * radius;
            const std::int64_t distanceSq =
                static_cast<std::int64_t>(dx) * dx + static_cast<std::int64_t>(dy) * dy;
            if (distanceSq >= radiusSq && distanceSq <= 4 * radiusSq) {
                return {dx, dy};
            }
        }
    }

    static PlacementHit hitFor(IRMath::ivec2 cell) {
        return {cell, fieldChunkOf(cell)};
    }

    // Grid coordinates are floor-divided cells, so they fit int32 whenever the
    // cell does; the packed key is the same two-int32 layout as a field chunk key.
    static std::uint64_t gridKey(std::int64_t gridX, std::int64_t gridY) {
        return packFieldChunkKey(
            {static_cast<std::int32_t>(gridX), static_cast<std::int32_t>(gridY)}
        );
    }

    void gridInsert(IRMath::ivec2 cell) {
        m_grid[gridKey(
            IRMath::floorDiv(cell.x, m_gridWidth),
            IRMath::floorDiv(cell.y, m_gridWidth)
        )] = cell;
    }

    // Scans the grid cells that can hold a sample closer than minSpacing; a
    // neighbour coordinate outside int32 can hold no sample and is skipped.
    bool spacingOk(IRMath::ivec2 candidate) const {
        const std::int64_t gridX = IRMath::floorDiv(candidate.x, m_gridWidth);
        const std::int64_t gridY = IRMath::floorDiv(candidate.y, m_gridWidth);
        for (int dy = -m_scanRadius; dy <= m_scanRadius; ++dy) {
            for (int dx = -m_scanRadius; dx <= m_scanRadius; ++dx) {
                const std::int64_t neighbourX = gridX + dx;
                const std::int64_t neighbourY = gridY + dy;
                if (!std::in_range<std::int32_t>(neighbourX) ||
                    !std::in_range<std::int32_t>(neighbourY)) {
                    continue;
                }
                const auto it = m_grid.find(gridKey(neighbourX, neighbourY));
                if (it == m_grid.end()) {
                    continue;
                }
                const std::int64_t deltaX = static_cast<std::int64_t>(it->second.x) - candidate.x;
                const std::int64_t deltaY = static_cast<std::int64_t>(it->second.y) - candidate.y;
                if (deltaX * deltaX + deltaY * deltaY < m_spacingSq) {
                    return false;
                }
            }
        }
        return true;
    }

    // Chunk-first: a present chunk whose clearance summary cannot reach the
    // requested clearance is rejected without a cell read, and an absent chunk
    // is occupied under the field's boundary rule. Each chunk's verdict is
    // memoised so the stats count distinct chunks.
    bool chunkMayHold(IRMath::ivec2 chunk) {
        auto [it, inserted] = m_chunkVerdicts.try_emplace(packFieldChunkKey(chunk), false);
        if (inserted) {
            ++m_stats.chunksConsidered_;
            const auto *summary = m_field.clearance().values().findChunk(chunk);
            if (summary != nullptr) {
                it->second = static_cast<std::int64_t>(summary->max_) >= m_clearanceSq;
                if (!it->second) {
                    ++m_stats.chunksPruned_;
                }
            }
        }
        return it->second;
    }

    bool isValid(IRMath::ivec2 cell) {
        if (!chunkMayHold(fieldChunkOf(cell))) {
            return false;
        }
        if (!m_field.clearance().hasClearance(cell, m_params.clearance_)) {
            return false;
        }
        return !m_params.sameRegionAsAnchor_ ||
               m_field.regions().sameRegion(cell, m_params.anchor_);
    }
};

} // namespace detail

// Clears `out`, zeroes `*stats`, and only then validates: a rejected query
// (std::invalid_argument, for a parameter outside the domain table or a
// field with pending changes) leaves an empty `out` and zero stats, and never
// touches draw state. On success `out` holds at most `params.k_` hits in
// acceptance order, anchor first when the anchor is itself a hit.
inline void queryPlacements(
    const PlacementField &field,
    const PlacementParams &params,
    std::vector<PlacementHit> &out,
    PlacementQueryStats *stats = nullptr
) {
    out.clear();
    if (stats != nullptr) {
        *stats = {};
    }
    detail::validatePlacementParams(field, params);

    detail::PlacementDraw draw(field, params);
    draw.run(out);
    if (stats != nullptr) {
        *stats = draw.stats();
    }
}

} // namespace IRPrefab::Spatial

#endif /* IR_FIELD_PLACEMENT_H */

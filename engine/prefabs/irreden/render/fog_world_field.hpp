#ifndef FOG_WORLD_FIELD_H
#define FOG_WORLD_FIELD_H

// The CPU authority for fog-of-war state: one cell per integer world column,
// unbounded, stored in 32×32 field chunks. GPU-free, so tests construct it
// without a render device. Contract: docs/design/fog-of-war-world-field.md
// (D1–D5, D8, D11, D13, D14).
//
// A cell carries a state byte (unexplored / explored / visible), a 32-bit
// channel mask (absent = `kFogChannelDefault`) and, under the DECAY policy,
// the simulation time it was last explored. The creation owns the clock
// (`setExploredTimeMs`); nothing here reads wall time. An EXPLORED cell whose
// mask intersects the policy mask returns to UNEXPLORED exactly when
// `now - lastExplored >= duration`; the persistent policy keeps it forever.
// Every eligible resident cell is expired when the clock advances, and an
// evicted cell is expired as its region reloads, before any reader sees it.
//
// With persistence set, every region (16×16 field chunks) is resident or not.
// The first read, write or gather expansion that touches a non-resident region
// probes its file once and loads what it holds before anything else happens,
// so a write never shadows a disk copy. A changed write marks the region
// persistence-dirty; a load does not. CPU access sets the region's access bit,
// which `evict` reads.
//
// Residency is serial (D13): every probe, load, write, expiry and access bit
// runs on the main thread. A `PARALLEL_FOR` tick reads through `peekCell`
// only, after its system's `beginTick` made the regions it will read resident
// with `touchCell`.
//
// Beside the persistent cells sits the transient vision-tier layer (D8): the
// union of the channel masks of every source past the analytic cap whose disc
// covers the cell, cleared with the vision set. It never persists, probes,
// evicts or sets an access bit. A cell reads visible through it only when that
// union intersects the cell's own mask, resolved at read and gather time.

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/spatial/chunked_field.hpp>
#include <irreden/system/ir_assert_main_thread.hpp>
#include <irreden/world/field_chunk_persistence.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
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
/// The fog layer's auxiliary region payloads: per-cell channel masks
/// (little-endian uint32) and last-exploration times (little-endian uint64
/// milliseconds), each written only for chunks that hold a non-default value.
constexpr std::array<char, 4> kFogFieldMaskTag{'F', 'M', 'A', 'S'};
constexpr std::array<char, 4> kFogFieldAgeTag{'F', 'A', 'G', 'E'};
constexpr int kFogFieldMaskBytesPerCell = 4;
constexpr int kFogFieldAgeBytesPerCell = 8;
/// Simulation clock values and decay durations are exact integers in
/// `[0, kFogTimeMsMax]`, the range a Lua number carries exactly.
constexpr std::uint64_t kFogTimeMsMax = (std::uint64_t{1} << 53) - 1;

/// What happens to EXPLORED memory: kept indefinitely, or returned to
/// UNEXPLORED after a creation-owned duration on the creation's clock.
enum class ExploredPolicy : int { PERSISTENT = 0, DECAY = 1 };

struct ExploredPolicySettings {
    ExploredPolicy policy_ = ExploredPolicy::PERSISTENT;
    std::uint64_t durationMs_ = 0;
    std::uint32_t channels_ = IRComponents::kFogChannelDefault;

    bool operator==(const ExploredPolicySettings &) const = default;
};

/// The fog layer's region persistence under @p saveRoot; `nullopt` for an
/// empty root.
inline std::optional<IRWorld::FieldChunkDiskPersistence>
createFieldPersistence(std::string saveRoot) {
    return IRWorld::FieldChunkDiskPersistence::create(
        std::move(saveRoot),
        kFogFieldLayer,
        kFogFieldBytesPerCell,
        {IRWorld::FieldRegionAuxSchema{kFogFieldMaskTag, kFogFieldMaskBytesPerCell},
         IRWorld::FieldRegionAuxSchema{kFogFieldAgeTag, kFogFieldAgeBytesPerCell}}
    );
}

/// The camera depth slab the GPU window is exact for: matter at
/// `z ∈ [-kFogWindowDepthHalfBand, kFogWindowDepthHalfBand)` that lands
/// anywhere on the canvas has its column inside the window.
constexpr int kFogWindowDepthHalfBand = 128;
/// Window edges are multiples of this (two field chunks) and never exceed
/// `kFogWindowEdgeMax` (128 MiB of RG32UI); a capped window over-fogs its
/// periphery.
constexpr int kFogWindowEdgeQuantum = 64;
constexpr int kFogWindowEdgeMax = 4096;
/// Cells the window adds past the covered radius: 32 for the chunk snap of
/// the origin and 1 for column rounding.
constexpr int kFogWindowSnapMargin = 33;
/// Field chunks the eviction keep rectangle extends past the window.
constexpr int kFogResidentMarginChunks = 4;

/// One texel of the GPU window: the effective state byte widened to 32 bits
/// (`max(persistent, transient)`) and the cell's exact channel mask. The
/// no-fog placeholder holds `(kFogStateVisible, kFogChannelDefault)`; a
/// column outside the window reads `(kFogStateUnexplored, kFogChannelDefault)`
/// in every shader tap.
struct FogWindowTexel {
    std::uint32_t state_ = IRComponents::kFogStateUnexplored;
    std::uint32_t channels_ = IRComponents::kFogChannelDefault;

    bool operator==(const FogWindowTexel &) const = default;
};
static_assert(sizeof(FogWindowTexel) == 8, "the fog window is RG32UI: two 32-bit lanes");

/// Resident counts now; probes, loads, saves, evictions and expired cells
/// since the previous `WorldField::stats()` call.
struct WorldFieldStats {
    int residentRegions_ = 0;
    int residentChunks_ = 0;
    int probes_ = 0;
    int loads_ = 0;
    int saves_ = 0;
    int evictions_ = 0;
    int expired_ = 0;
};

class WorldField {
  public:
    using Cells = IRPrefab::Spatial::ChunkedField2D<std::uint8_t>;
    using MaskCells = IRPrefab::Spatial::ChunkedField2D<std::uint32_t>;
    using AgeCells = IRPrefab::Spatial::ChunkedField2D<std::uint64_t>;

    /// False once any field chunk is present: a late root would shadow disk
    /// state, and the field never merges.
    bool setPersistence(IRWorld::FieldChunkDiskPersistence persistence) {
        if (anyChunkPresent()) {
            return false;
        }
        m_persistence.emplace(std::move(persistence));
        m_regions.clear();
        return true;
    }

    bool hasPersistence() const {
        return m_persistence.has_value();
    }

    /// Selects the explored-state policy. Initialization-only: once the field
    /// holds a cell or a region record, only a repeat of the current settings
    /// is accepted; `clear()` permits reconfiguration. DECAY requires a
    /// duration in `[1, kFogTimeMsMax]`, PERSISTENT one in `[0, kFogTimeMsMax]`.
    /// A rejected call changes nothing.
    bool setExploredPolicy(
        ExploredPolicy policy,
        std::uint64_t durationMs,
        std::uint32_t channels = IRComponents::kFogChannelDefault
    ) {
        if (policy != ExploredPolicy::PERSISTENT && policy != ExploredPolicy::DECAY) {
            return false;
        }
        if (durationMs > kFogTimeMsMax || (policy == ExploredPolicy::DECAY && durationMs == 0)) {
            return false;
        }
        const ExploredPolicySettings requested{policy, durationMs, channels};
        if (requested == m_policy) {
            return true;
        }
        if (anyChunkPresent() || !m_regions.empty()) {
            return false;
        }
        m_policy = requested;
        return true;
    }

    ExploredPolicySettings exploredPolicy() const {
        return m_policy;
    }

    bool decays() const {
        return m_policy.policy_ == ExploredPolicy::DECAY;
    }

    /// Advances the simulation clock to @p nowMs, expiring every resident
    /// eligible EXPLORED cell that is due under DECAY. Equal time is a no-op;
    /// a backwards or out-of-range time is rejected without mutation. Serial.
    bool setExploredTimeMs(std::uint64_t nowMs) {
        if (nowMs > kFogTimeMsMax || nowMs < m_nowMs) {
            return false;
        }
        if (nowMs == m_nowMs) {
            return true;
        }
        IR_ASSERT_MAIN_THREAD();
        m_nowMs = nowMs;
        if (decays()) {
            expireDue();
        }
        return true;
    }

    std::uint64_t exploredTimeMs() const {
        return m_nowMs;
    }

    /// `max(persistent, transient)`; absent cells read `kFogStateUnexplored`.
    /// Not const: the read can load the cell's region.
    std::uint8_t getCell(IRMath::ivec2 cell) {
        touchRegion(regionOfCell(cell), true);
        return composeCell(cell);
    }

    /// `max(persistent, transient)` over the layers whose field chunk holding
    /// @p cell is in memory, or nullopt when neither is. Never probes and
    /// never sets the access bit, so a fixture can observe residency without
    /// changing what the next eviction drops.
    std::optional<std::uint8_t> peekCell(IRMath::ivec2 cell) const {
        const IRMath::ivec2 chunk = IRPrefab::Spatial::fieldChunkOf(cell);
        if (m_cells.findChunk(chunk) == nullptr && m_transient.findChunk(chunk) == nullptr) {
            return std::nullopt;
        }
        return composeCell(cell);
    }

    /// The channel mask of @p cell; absent metadata reads
    /// `kFogChannelDefault`. Loads the cell's region.
    std::uint32_t getCellChannels(IRMath::ivec2 cell) {
        touchRegion(regionOfCell(cell), true);
        return cellChannels(cell);
    }

    /// The resident channel mask of @p cell (`peekCell`'s discipline).
    std::uint32_t peekCellChannels(IRMath::ivec2 cell) const {
        return cellChannels(cell);
    }

    /// Makes @p cell's region resident and sets its access bit, as `getCell`
    /// would, without reading: the residency pre-pass of a parallel reader
    /// (D13). A no-op without persistence.
    void touchCell(IRMath::ivec2 cell) {
        touchRegion(regionOfCell(cell), true);
    }

    /// The region holding @p cell: residency is per region, so two cells
    /// with one region share every touch.
    static IRMath::ivec2 regionOfCell(IRMath::ivec2 cell) {
        return IRWorld::FieldChunkDiskPersistence::regionOf(IRPrefab::Spatial::fieldChunkOf(cell));
    }

    /// Writes the persistent layer only: under a transient disc the cell
    /// still reads visible. An EXPLORED write under DECAY records the clock
    /// as the cell's exploration time even when the state byte is unchanged;
    /// any other state clears it. Returns whether the state changed.
    bool setCell(IRMath::ivec2 cell, std::uint8_t state) {
        RegionRecord *record = touchRegion(regionOfCell(cell), true);
        if (decays() && writeAge(cell, state)) {
            markPersistenceDirty(record);
        }
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
    /// returns the changed-cell count, with `setCell`'s age bookkeeping.
    /// Requires `count >= 0` and the run representable in int32.
    int fillRow(IRMath::ivec2 firstCell, int count, std::uint8_t state) {
        int changed = 0;
        forEachRegionRun(firstCell, count, [&](IRMath::ivec2 runFirst, int run) {
            RegionRecord *record = touchRegion(regionOfCell(runFirst), true);
            if (decays() && writeAgeRow(runFirst, run, state)) {
                markPersistenceDirty(record);
            }
            const int runChanged = m_cells.fillRow(runFirst, run, state);
            if (runChanged > 0) {
                markPersistenceDirty(record);
            }
            changed += runChanged;
        });
        return changed;
    }

    /// Replaces @p cell's channel mask and returns whether it changed. Under
    /// DECAY the cell's pending expiry is resolved under the old mask first,
    /// then under the new one, so removing a bit never resurrects expired
    /// memory and a newly eligible old cell expires at once. The mask does not
    /// refresh the cell's age. Loads the cell's region.
    bool setCellChannels(IRMath::ivec2 cell, std::uint32_t channels) {
        RegionRecord *record = touchRegion(regionOfCell(cell), true);
        if (decays()) {
            expireCellIfDue(cell);
        }
        const std::uint32_t encoded = channels ^ IRComponents::kFogChannelDefault;
        const IRMath::ivec2 chunk = IRPrefab::Spatial::fieldChunkOf(cell);
        bool changed = false;
        if (encoded != 0 || m_masks.findChunk(chunk) != nullptr) {
            changed = m_masks.setCell(cell, encoded);
        }
        if (changed) {
            markPersistenceDirty(record);
        }
        if (decays()) {
            std::uint8_t state = IRComponents::kFogStateUnexplored;
            if (m_cells.getCell(cell, state) && state == IRComponents::kFogStateExplored) {
                noteDecayCandidate(cell);
                expireCellIfDue(cell);
            }
        }
        return changed;
    }

    /// Marks every cell whose centre lies within @p radius of @p centre
    /// (`dx² + dy² <= r²`) and whose mask intersects @p channels visible, and
    /// returns the changed-cell count. The radius clamps to
    /// `kFogRevealRadiusMax`; the part of the disc beyond the int32 range is
    /// skipped. Cells outside the disc are never downgraded.
    int revealRadius(
        IRMath::ivec2 centre, int radius, std::uint32_t channels = IRComponents::kFogChannelDefault
    ) {
        if (radius < 0) {
            return 0;
        }
        const std::int64_t r = IRMath::min(radius, kFogRevealRadiusMax);
        int changed = 0;
        forEachDiscRow(centre, r, r * r, [&](IRMath::ivec2 firstCell, int count) {
            changed += fillRowAdmitted(firstCell, count, channels, IRComponents::kFogStateVisible);
        });
        return changed;
    }

    /// Authors explored memory: every cell of the disc (`revealRadius`'s
    /// metric) whose mask intersects @p channels and is not VISIBLE becomes
    /// EXPLORED, refreshing its exploration time; VISIBLE cells are retained.
    /// Returns the changed-cell count (a refresh alone is not a change).
    int exploreRadius(
        IRMath::ivec2 centre, int radius, std::uint32_t channels = IRComponents::kFogChannelDefault
    ) {
        if (radius < 0) {
            return 0;
        }
        const std::int64_t r = IRMath::min(radius, kFogRevealRadiusMax);
        int changed = 0;
        forEachDiscRow(centre, r, r * r, [&](IRMath::ivec2 firstCell, int count) {
            changed += fillRowAdmitted(firstCell, count, channels, IRComponents::kFogStateExplored);
        });
        return changed;
    }

    /// Stamps the vision-tier disc of a source carrying @p channels at world
    /// point @p centre into the transient layer: every cell whose centre lies
    /// within @p radius of `roundHalfUp(centre)` (`dx² + dy² <= radius²`, the
    /// `revealRadius` metric) gains those bits until `clearTransient`, and
    /// reads visible while they intersect its own mask. The radius clamps to
    /// `kFogRevealRadiusMax`; a non-positive one or a zero mask stamps
    /// nothing. Returns the changed-cell count.
    int stampTransientDisc(
        IRMath::vec2 centre, float radius, std::uint32_t channels = IRComponents::kFogChannelDefault
    ) {
        if (!(radius > 0.0f) || channels == 0u) {
            return 0;
        }
        const float clamped = IRMath::min(radius, static_cast<float>(kFogRevealRadiusMax));
        const IRMath::ivec2 cell{IRMath::roundHalfUp(centre.x), IRMath::roundHalfUp(centre.y)};
        const auto radiusSquared = static_cast<std::int64_t>(IRMath::floor(clamped * clamped));
        const auto rowRadius = static_cast<std::int64_t>(IRMath::floor(clamped));
        int changed = 0;
        forEachDiscRow(cell, rowRadius, radiusSquared, [&](IRMath::ivec2 firstCell, int count) {
            changed += m_transient.orRow(firstCell, count, channels);
        });
        return changed;
    }

    /// Drops every transient disc. The persistent layer, the regions and the
    /// persistence handle are untouched.
    void clearTransient() {
        m_transient.clear();
    }

    /// Resets every persistent cell to unexplored and drops all cell metadata;
    /// the transient layer is the vision set's and survives, and the clock and
    /// policy stand (the policy may now be reconfigured). With persistence it
    /// also deletes the layer's region files and forgets every region, so the
    /// next access probes again.
    void clear() {
        m_cells.clear();
        m_masks.clear();
        m_ages.clear();
        m_decayCandidates.clear();
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
                    const IRMath::ivec2 chunk = firstChunk + IRMath::ivec2{lx, ly};
                    m_cells.eraseChunk(chunk);
                    m_masks.eraseChunk(chunk);
                    m_ages.eraseChunk(chunk);
                    m_decayCandidates.erase(IRPrefab::Spatial::packFieldChunkKey(chunk));
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

    /// The transient layer's field chunk of source-mask unions, or null when
    /// absent. Invalidated by any later stamp or `clearTransient`.
    const MaskCells::FieldChunk *findTransientChunk(IRMath::ivec2 chunkCoord) const {
        return m_transient.findChunk(chunkCoord);
    }

    /// The channel-mask field chunk (cells store `mask ^ kFogChannelDefault`,
    /// so an absent chunk or a zero cell is the default mask), or null when
    /// absent. The region is the one `findChunkForGather` just made resident.
    const MaskCells::FieldChunk *findMaskChunk(IRMath::ivec2 chunkCoord) const {
        return m_masks.findChunk(chunkCoord);
    }

    /// Replaces @p out with the field chunks any layer the window shows
    /// changed since the previous call (sorted, unique) and refreshes their
    /// summaries. The only drain of the pending set; an undrained field grows
    /// it. The caller retains capacity for the largest union of consecutive
    /// calls' changed chunks, so a repeating workload is warm only after every
    /// transition has run.
    void consumePending(std::vector<IRPrefab::Spatial::FieldChunkKey> &out) {
        m_cells.dirtyKeys(out);
        m_cells.update();
        bool merged = false;
        if (m_transient.hasDirtyKeys()) {
            m_transient.dirtyKeys(m_keysScratch);
            m_transient.update();
            out.insert(out.end(), m_keysScratch.begin(), m_keysScratch.end());
            merged = true;
        }
        if (m_masks.hasDirtyKeys()) {
            m_masks.dirtyKeys(m_keysScratch);
            m_masks.update();
            out.insert(out.end(), m_keysScratch.begin(), m_keysScratch.end());
            merged = true;
        }
        if (m_ages.hasDirtyKeys()) {
            m_ages.update();
        }
        if (!merged) {
            return;
        }
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

    /// Field chunks holding channel masks or exploration times, for memory
    /// accounting.
    std::size_t metadataChunkCount() const {
        return m_masks.chunkCount() + m_ages.chunkCount();
    }

  private:
    static constexpr int kRegionEdgeCells =
        IRWorld::kFieldRegionEdgeChunks * IRPrefab::Spatial::kFieldChunkEdge;
    static constexpr std::size_t kMaskPayload = 0;
    static constexpr std::size_t kAgePayload = 1;
    static constexpr std::uint64_t kNoCandidate = std::numeric_limits<std::uint64_t>::max();

    struct RegionRecord {
        bool accessed_ = false;
        bool persistenceDirty_ = false;
    };

    Cells m_cells;
    MaskCells m_transient;
    MaskCells m_masks;
    AgeCells m_ages;
    /// Chunks that may hold a decay-eligible EXPLORED cell, with a lower bound
    /// on the earliest such cell's exploration time. A bound below the truth
    /// only costs a scan, which recomputes it; the clock never has to visit a
    /// chunk whose bound is not yet due.
    std::unordered_map<IRPrefab::Spatial::FieldChunkKey, std::uint64_t> m_decayCandidates;
    ExploredPolicySettings m_policy;
    std::uint64_t m_nowMs = 0;
    std::vector<IRPrefab::Spatial::FieldChunkKey> m_keysScratch;
    std::optional<IRWorld::FieldChunkDiskPersistence> m_persistence;
    std::unordered_map<IRPrefab::Spatial::FieldChunkKey, RegionRecord> m_regions;
    std::vector<IRPrefab::Spatial::FieldChunkKey> m_evictScratch;
    IRWorld::FieldRegion m_regionScratch;
    std::array<std::uint32_t, IRPrefab::Spatial::kFieldChunkCells> m_maskChunkScratch{};
    std::array<std::uint64_t, IRPrefab::Spatial::kFieldChunkCells> m_ageChunkScratch{};
    WorldFieldStats m_counters;

    bool anyChunkPresent() const {
        return m_cells.chunkCount() != 0 || m_masks.chunkCount() != 0 || m_ages.chunkCount() != 0;
    }

    std::uint32_t cellChannels(IRMath::ivec2 cell) const {
        std::uint32_t encoded = 0;
        if (!m_masks.getCell(cell, encoded)) {
            return IRComponents::kFogChannelDefault;
        }
        return encoded ^ IRComponents::kFogChannelDefault;
    }

    /// `max(persistent, transient)` over resident data, the transient term
    /// admitted only when its source union intersects the cell's mask.
    std::uint8_t composeCell(IRMath::ivec2 cell) const {
        std::uint8_t state = IRComponents::kFogStateUnexplored;
        m_cells.getCell(cell, state);
        std::uint32_t transient = 0;
        if (m_transient.getCell(cell, transient) && (transient & cellChannels(cell)) != 0u) {
            return IRComponents::kFogStateVisible;
        }
        return state;
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

    /// Splits the +x run `[firstCell.x, firstCell.x + count)` at region edges
    /// (or passes it whole without persistence) and calls @p run per piece.
    template <typename RunFn>
    void forEachRegionRun(IRMath::ivec2 firstCell, int count, RunFn &&run) {
        if (!m_persistence.has_value()) {
            if (count > 0) {
                run(firstCell, count);
            }
            return;
        }
        int x = firstCell.x;
        int remaining = count;
        while (remaining > 0) {
            const int regionLocalX = x & (kRegionEdgeCells - 1);
            const int piece = IRMath::min(remaining, kRegionEdgeCells - regionLocalX);
            run(IRMath::ivec2{x, firstCell.y}, piece);
            remaining -= piece;
            if (remaining > 0) {
                x += piece;
            }
        }
    }

    /// Splits the +x run at field-chunk edges and calls @p run(firstCell,
    /// count, chunkCoord) per piece.
    template <typename RunFn>
    static void forEachChunkRun(IRMath::ivec2 firstCell, int count, RunFn &&run) {
        int x = firstCell.x;
        int remaining = count;
        while (remaining > 0) {
            const IRMath::ivec2 cell{x, firstCell.y};
            const int localX = IRPrefab::Spatial::fieldChunkLocal(cell).x;
            const int piece = IRMath::min(remaining, IRPrefab::Spatial::kFieldChunkEdge - localX);
            run(cell, piece, IRPrefab::Spatial::fieldChunkOf(cell));
            remaining -= piece;
            if (remaining > 0) {
                x += piece;
            }
        }
    }

    /// `fillRow` restricted to cells whose mask intersects @p channels; for
    /// EXPLORED, VISIBLE cells are retained. Returns the changed-cell count.
    int fillRowAdmitted(
        IRMath::ivec2 firstCell, int count, std::uint32_t channels, std::uint8_t state
    ) {
        int changed = 0;
        const bool exploring = state == IRComponents::kFogStateExplored;
        forEachRegionRun(firstCell, count, [&](IRMath::ivec2 regionFirst, int regionCount) {
            RegionRecord *record = touchRegion(regionOfCell(regionFirst), true);
            forEachChunkRun(
                regionFirst,
                regionCount,
                [&](IRMath::ivec2 chunkFirst, int run, IRMath::ivec2 chunk) {
                    const MaskCells::FieldChunk *masks = m_masks.findChunk(chunk);
                    const Cells::FieldChunk *states =
                        exploring ? m_cells.findChunk(chunk) : nullptr;
                    if (masks == nullptr && states == nullptr) {
                        if ((channels & IRComponents::kFogChannelDefault) == 0u) {
                            return;
                        }
                        changed += writeAdmittedRun(record, chunkFirst, run, state);
                        return;
                    }
                    // Coalesce the admitted cells of the run into sub-runs.
                    int subStart = -1;
                    for (int i = 0; i <= run; ++i) {
                        bool admitted = false;
                        if (i < run) {
                            const IRMath::ivec2 cell = chunkFirst + IRMath::ivec2{i, 0};
                            const int local = IRPrefab::Spatial::fieldChunkLocalIndex(
                                IRPrefab::Spatial::fieldChunkLocal(cell)
                            );
                            const std::uint32_t mask =
                                masks == nullptr
                                    ? IRComponents::kFogChannelDefault
                                    : (masks->cells()[static_cast<std::size_t>(local)] ^
                                       IRComponents::kFogChannelDefault);
                            admitted = (mask & channels) != 0u;
                            if (admitted && states != nullptr &&
                                states->cells()[static_cast<std::size_t>(local)] ==
                                    IRComponents::kFogStateVisible) {
                                admitted = false;
                            }
                        }
                        if (admitted) {
                            if (subStart < 0) {
                                subStart = i;
                            }
                            continue;
                        }
                        if (subStart >= 0) {
                            changed += writeAdmittedRun(
                                record,
                                chunkFirst + IRMath::ivec2{subStart, 0},
                                i - subStart,
                                state
                            );
                            subStart = -1;
                        }
                    }
                }
            );
        });
        return changed;
    }

    int
    writeAdmittedRun(RegionRecord *record, IRMath::ivec2 firstCell, int count, std::uint8_t state) {
        if (decays() && writeAgeRow(firstCell, count, state)) {
            markPersistenceDirty(record);
        }
        const int changed = m_cells.fillRow(firstCell, count, state);
        if (changed > 0) {
            markPersistenceDirty(record);
        }
        return changed;
    }

    /// The exploration-time bookkeeping of a @p state write at @p cell under
    /// DECAY: an EXPLORED write records the clock (a cell explored at epoch
    /// zero is the all-zero default) and makes its chunk a decay candidate;
    /// any other state clears the recorded time. Never inserts a chunk to hold
    /// a zero. Returns whether stored metadata changed.
    bool writeAge(IRMath::ivec2 cell, std::uint8_t state) {
        const IRMath::ivec2 chunk = IRPrefab::Spatial::fieldChunkOf(cell);
        const bool explored = state == IRComponents::kFogStateExplored;
        const std::uint64_t ageMs = explored ? m_nowMs : 0;
        if (explored) {
            noteDecayCandidate(chunk, ageMs);
        }
        if (ageMs == 0 && m_ages.findChunk(chunk) == nullptr) {
            return false;
        }
        return m_ages.setCell(cell, ageMs);
    }

    bool writeAgeRow(IRMath::ivec2 firstCell, int count, std::uint8_t state) {
        const bool explored = state == IRComponents::kFogStateExplored;
        const std::uint64_t ageMs = explored ? m_nowMs : 0;
        bool changed = false;
        forEachChunkRun(
            firstCell,
            count,
            [&](IRMath::ivec2 chunkFirst, int run, IRMath::ivec2 chunk) {
                if (explored) {
                    noteDecayCandidate(chunk, ageMs);
                }
                if (ageMs == 0 && m_ages.findChunk(chunk) == nullptr) {
                    return;
                }
                changed = m_ages.fillRow(chunkFirst, run, ageMs) > 0 || changed;
            }
        );
        return changed;
    }

    /// Lowers @p chunk's candidate bound to @p ageMs, entering it when absent.
    void noteDecayCandidate(IRMath::ivec2 chunk, std::uint64_t ageMs) {
        auto [it, inserted] =
            m_decayCandidates.try_emplace(IRPrefab::Spatial::packFieldChunkKey(chunk), ageMs);
        it->second = IRMath::min(it->second, ageMs);
    }

    void noteDecayCandidate(IRMath::ivec2 cell) {
        std::uint64_t age = 0;
        m_ages.getCell(cell, age);
        noteDecayCandidate(IRPrefab::Spatial::fieldChunkOf(cell), age);
    }

    /// Whether an EXPLORED cell of @p mask explored at @p age is due now.
    bool cellDue(std::uint8_t state, std::uint32_t mask, std::uint64_t age) const {
        return state == IRComponents::kFogStateExplored && (mask & m_policy.channels_) != 0u &&
               m_nowMs - age >= m_policy.durationMs_;
    }

    /// Expires @p cell now when it is due under its current mask.
    void expireCellIfDue(IRMath::ivec2 cell) {
        std::uint8_t state = IRComponents::kFogStateUnexplored;
        if (!m_cells.getCell(cell, state)) {
            return;
        }
        std::uint64_t age = 0;
        m_ages.getCell(cell, age);
        if (!cellDue(state, cellChannels(cell), age)) {
            return;
        }
        m_cells.setCell(cell, IRComponents::kFogStateUnexplored);
        if (age != 0) {
            m_ages.setCell(cell, 0);
        }
        ++m_counters.expired_;
        markPersistenceDirty(touchRegion(regionOfCell(cell), false));
    }

    /// Expires every due cell of every candidate chunk whose bound is due.
    void expireDue() {
        for (auto it = m_decayCandidates.begin(); it != m_decayCandidates.end();) {
            if (m_nowMs - it->second < m_policy.durationMs_) {
                ++it;
                continue;
            }
            const std::uint64_t earliest =
                expireChunk(IRPrefab::Spatial::unpackFieldChunkKey(it->first));
            if (earliest == kNoCandidate) {
                it = m_decayCandidates.erase(it);
            } else {
                it->second = earliest;
                ++it;
            }
        }
    }

    /// Re-derives @p chunk's candidate entry from a scan that also expires
    /// its due cells.
    void rescanDecayCandidate(IRMath::ivec2 chunk) {
        const IRPrefab::Spatial::FieldChunkKey key = IRPrefab::Spatial::packFieldChunkKey(chunk);
        const std::uint64_t earliest = expireChunk(chunk);
        if (earliest == kNoCandidate) {
            m_decayCandidates.erase(key);
        } else {
            m_decayCandidates[key] = earliest;
        }
    }

    /// Expires the due cells of @p chunk and returns the earliest exploration
    /// time still eligible to decay there, or `kNoCandidate` when none
    /// remains. A cell with no recorded time was explored at epoch zero.
    std::uint64_t expireChunk(IRMath::ivec2 chunk) {
        using IRPrefab::Spatial::kFieldChunkEdge;
        const Cells::FieldChunk *states = m_cells.findChunk(chunk);
        if (states == nullptr) {
            return kNoCandidate;
        }
        const AgeCells::FieldChunk *ages = m_ages.findChunk(chunk);
        const MaskCells::FieldChunk *masks = m_masks.findChunk(chunk);
        const IRMath::ivec2 firstCell = chunk * kFieldChunkEdge;
        std::uint64_t earliest = kNoCandidate;
        int expired = 0;
        for (int y = 0; y < kFieldChunkEdge; ++y) {
            int runStart = -1;
            for (int x = 0; x <= kFieldChunkEdge; ++x) {
                bool due = false;
                if (x < kFieldChunkEdge) {
                    const auto i = static_cast<std::size_t>(y * kFieldChunkEdge + x);
                    const std::uint64_t age = ages == nullptr ? 0 : ages->cells()[i];
                    const std::uint32_t mask =
                        masks == nullptr ? IRComponents::kFogChannelDefault
                                         : (masks->cells()[i] ^ IRComponents::kFogChannelDefault);
                    const std::uint8_t state = states->cells()[i];
                    const bool eligible = state == IRComponents::kFogStateExplored &&
                                          (mask & m_policy.channels_) != 0u;
                    due = eligible && m_nowMs - age >= m_policy.durationMs_;
                    if (eligible && !due) {
                        earliest = IRMath::min(earliest, age);
                    }
                }
                if (due) {
                    if (runStart < 0) {
                        runStart = x;
                    }
                    continue;
                }
                if (runStart >= 0) {
                    const IRMath::ivec2 runFirst = firstCell + IRMath::ivec2{runStart, y};
                    const int run = x - runStart;
                    m_cells.fillRow(runFirst, run, IRComponents::kFogStateUnexplored);
                    if (ages != nullptr) {
                        m_ages.fillRow(runFirst, run, 0);
                    }
                    expired += run;
                    runStart = -1;
                }
            }
        }
        if (expired > 0) {
            m_counters.expired_ += expired;
            markPersistenceDirty(
                touchRegion(IRWorld::FieldChunkDiskPersistence::regionOf(chunk), false)
            );
        }
        return earliest;
    }

    static void markPersistenceDirty(RegionRecord *record) {
        if (record != nullptr) {
            record->persistenceDirty_ = true;
        }
    }

    RegionRecord *touchRegion(IRMath::ivec2 region, bool cpuAccess) {
        IR_ASSERT_MAIN_THREAD();
        if (!m_persistence.has_value()) {
            return nullptr;
        }
        const IRPrefab::Spatial::FieldChunkKey key = IRPrefab::Spatial::packFieldChunkKey(region);
        auto [it, inserted] = m_regions.try_emplace(key);
        RegionRecord &record = it->second;
        record.accessed_ = record.accessed_ || cpuAccess;
        if (inserted) {
            loadRegion(region, record);
        }
        return &record;
    }

    static std::uint32_t readLittleEndian32(const std::uint8_t *bytes) {
        return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
               (static_cast<std::uint32_t>(bytes[2]) << 16) |
               (static_cast<std::uint32_t>(bytes[3]) << 24);
    }

    static std::uint64_t readLittleEndian64(const std::uint8_t *bytes) {
        return static_cast<std::uint64_t>(readLittleEndian32(bytes)) |
               (static_cast<std::uint64_t>(readLittleEndian32(bytes + 4)) << 32);
    }

    static void writeLittleEndian32(std::vector<std::uint8_t> &out, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    static void writeLittleEndian64(std::vector<std::uint8_t> &out, std::uint64_t value) {
        writeLittleEndian32(out, static_cast<std::uint32_t>(value));
        writeLittleEndian32(out, static_cast<std::uint32_t>(value >> 32));
    }

    static IRMath::ivec2 regionLocalChunk(int bit) {
        return {bit % IRWorld::kFieldRegionEdgeChunks, bit / IRWorld::kFieldRegionEdgeChunks};
    }

    /// Installs a loaded region. Every payload is validated before the first
    /// cell lands: an exploration time beyond the clock makes the whole region
    /// read empty with a warning rather than installing part of it.
    void loadRegion(IRMath::ivec2 region, RegionRecord &record) {
        using IRPrefab::Spatial::kFieldChunkCells;
        ++m_counters.probes_;
        std::optional<IRWorld::FieldRegion> loaded = m_persistence->loadRegion(region);
        if (!loaded.has_value()) {
            return;
        }
        const IRWorld::FieldRegionAux *maskPayload =
            loaded->aux_.size() > kMaskPayload ? &loaded->aux_[kMaskPayload] : nullptr;
        const IRWorld::FieldRegionAux *agePayload =
            loaded->aux_.size() > kAgePayload && decays() ? &loaded->aux_[kAgePayload] : nullptr;
        if (agePayload != nullptr) {
            for (std::size_t i = 0; i + kFogFieldAgeBytesPerCell <= agePayload->cells_.size();
                 i += kFogFieldAgeBytesPerCell) {
                if (readLittleEndian64(agePayload->cells_.data() + i) > m_nowMs) {
                    IRE_LOG_WARN(
                        "WorldField: fog region {},{} records an exploration time beyond the "
                        "restored clock {}; reading it as empty",
                        region.x,
                        region.y,
                        m_nowMs
                    );
                    return;
                }
            }
        }
        ++m_counters.loads_;
        const IRMath::ivec2 firstChunk =
            IRWorld::FieldChunkDiskPersistence::regionFirstChunk(region);
        const bool importLegacy =
            decays() && loaded->version_ < IRWorld::kFieldRegionVersionAuxiliary;
        std::size_t stateOffset = 0;
        for (int bit = 0; bit < IRWorld::kFieldRegionChunks; ++bit) {
            if (!loaded->hasChunk(bit)) {
                continue;
            }
            const IRMath::ivec2 chunk = firstChunk + regionLocalChunk(bit);
            const std::span<const std::uint8_t, kFieldChunkCells> states{
                loaded->cells_.data() + stateOffset,
                kFieldChunkCells
            };
            stateOffset += kFieldChunkCells;
            m_cells.assignChunk(chunk, states);
            if (importLegacy) {
                // A pre-metadata save never recorded exploration times: the
                // restored clock is the first known one, saved back so a later
                // reload does not renew it again.
                bool explored = false;
                for (std::size_t i = 0; i < kFieldChunkCells; ++i) {
                    const bool isExplored = states[i] == IRComponents::kFogStateExplored;
                    m_ageChunkScratch[i] = isExplored ? m_nowMs : 0;
                    explored = explored || isExplored;
                }
                if (explored && m_nowMs != 0) {
                    m_ages.assignChunk(chunk, m_ageChunkScratch);
                }
                record.persistenceDirty_ = true;
            }
        }
        if (maskPayload != nullptr) {
            std::size_t offset = 0;
            for (int bit = 0; bit < IRWorld::kFieldRegionChunks; ++bit) {
                if (!maskPayload->hasChunk(bit)) {
                    continue;
                }
                bool nonDefault = false;
                for (std::size_t i = 0; i < kFieldChunkCells; ++i) {
                    const std::uint32_t mask = readLittleEndian32(
                        maskPayload->cells_.data() + offset + i * kFogFieldMaskBytesPerCell
                    );
                    m_maskChunkScratch[i] = mask ^ IRComponents::kFogChannelDefault;
                    nonDefault = nonDefault || m_maskChunkScratch[i] != 0u;
                }
                offset += kFieldChunkCells * kFogFieldMaskBytesPerCell;
                if (nonDefault) {
                    m_masks.assignChunk(firstChunk + regionLocalChunk(bit), m_maskChunkScratch);
                }
            }
        }
        if (agePayload != nullptr) {
            std::size_t offset = 0;
            for (int bit = 0; bit < IRWorld::kFieldRegionChunks; ++bit) {
                if (!agePayload->hasChunk(bit)) {
                    continue;
                }
                std::uint64_t earliest = 0;
                for (std::size_t i = 0; i < kFieldChunkCells; ++i) {
                    const std::uint64_t age = readLittleEndian64(
                        agePayload->cells_.data() + offset + i * kFogFieldAgeBytesPerCell
                    );
                    m_ageChunkScratch[i] = age;
                    if (age != 0) {
                        earliest = earliest == 0 ? age : IRMath::min(earliest, age);
                    }
                }
                offset += kFieldChunkCells * kFogFieldAgeBytesPerCell;
                if (earliest != 0) {
                    m_ages.assignChunk(firstChunk + regionLocalChunk(bit), m_ageChunkScratch);
                }
            }
        }
        if (!decays()) {
            return;
        }
        // Age the region's cells off-camera: whatever fell due while the
        // region was out of memory expires before any reader sees it, and
        // each loaded chunk enters the candidate set with its true bound. An
        // explored cell no save recorded a time for was explored at epoch
        // zero.
        for (int bit = 0; bit < IRWorld::kFieldRegionChunks; ++bit) {
            if (loaded->hasChunk(bit)) {
                rescanDecayCandidate(firstChunk + regionLocalChunk(bit));
            }
        }
    }

    bool saveRegion(IRMath::ivec2 region) {
        using IRPrefab::Spatial::kFieldChunkCells;
        IRWorld::FieldRegion &data = m_regionScratch;
        data.mask_.fill(0);
        data.cells_.clear();
        data.aux_.resize(2);
        for (IRWorld::FieldRegionAux &aux : data.aux_) {
            aux.mask_.fill(0);
            aux.cells_.clear();
        }
        const IRMath::ivec2 firstChunk =
            IRWorld::FieldChunkDiskPersistence::regionFirstChunk(region);
        for (int bit = 0; bit < IRWorld::kFieldRegionChunks; ++bit) {
            const IRMath::ivec2 chunk = firstChunk + regionLocalChunk(bit);
            const Cells::FieldChunk *states = m_cells.findChunk(chunk);
            const MaskCells::FieldChunk *masks = m_masks.findChunk(chunk);
            const AgeCells::FieldChunk *ages = decays() ? m_ages.findChunk(chunk) : nullptr;
            const bool maskPayload = masks != nullptr && masks->nonZeroCount_ > 0;
            const bool agePayload = ages != nullptr && ages->nonZeroCount_ > 0;
            if (states == nullptr && !maskPayload && !agePayload) {
                continue;
            }
            data.setChunk(bit);
            if (states != nullptr) {
                data.cells_
                    .insert(data.cells_.end(), states->cells().begin(), states->cells().end());
            } else {
                data.cells_
                    .insert(data.cells_.end(), kFieldChunkCells, IRComponents::kFogStateUnexplored);
            }
            if (maskPayload) {
                data.aux_[kMaskPayload].setChunk(bit);
                for (std::uint32_t encoded : masks->cells()) {
                    writeLittleEndian32(
                        data.aux_[kMaskPayload].cells_,
                        encoded ^ IRComponents::kFogChannelDefault
                    );
                }
            }
            if (agePayload) {
                data.aux_[kAgePayload].setChunk(bit);
                for (std::uint64_t age : ages->cells()) {
                    writeLittleEndian64(data.aux_[kAgePayload].cells_, age);
                }
            }
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

/// The window edge for a fog canvas of @p canvasSize trixels:
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

/// Writes @p rect's cells into @p scratch as rows of `rect.size_.x`
/// `FogWindowTexel`s — the effective state (`max(persistent, transient)`,
/// the transient term admitted only where its source union intersects the
/// cell's mask) and the cell's mask — making each covered region resident
/// first. Each texture chunk shows the in-window field chunk at its toroidal
/// address for the window at @p origin. @p scratch holds at least
/// `rect.size_.x * rect.size_.y` texels.
inline void expandWindowChunks(
    WorldField &field,
    IRMath::ivec2 origin,
    int edge,
    const WindowUploadRect &rect,
    std::span<FogWindowTexel> scratch
) {
    using IRPrefab::Spatial::kFieldChunkEdge;
    const IRMath::ivec2 originChunk = IRPrefab::Spatial::fieldChunkOf(origin);
    const int edgeChunks = edge / kFieldChunkEdge;
    const IRMath::ivec2 firstTextureChunk = rect.texel_ / kFieldChunkEdge;
    const IRMath::ivec2 chunkExtent = rect.size_ / kFieldChunkEdge;
    const auto rowTexels = static_cast<std::size_t>(rect.size_.x);
    for (int chunkRow = 0; chunkRow < chunkExtent.y; ++chunkRow) {
        for (int chunkColumn = 0; chunkColumn < chunkExtent.x; ++chunkColumn) {
            const IRMath::ivec2 chunkCoord = windowChunkOfTextureChunk(
                originChunk,
                firstTextureChunk + IRMath::ivec2{chunkColumn, chunkRow},
                edgeChunks
            );
            const WorldField::Cells::FieldChunk *fieldChunk = field.findChunkForGather(chunkCoord);
            const WorldField::MaskCells::FieldChunk *transientChunk =
                field.findTransientChunk(chunkCoord);
            const WorldField::MaskCells::FieldChunk *maskChunk = field.findMaskChunk(chunkCoord);
            for (int y = 0; y < kFieldChunkEdge; ++y) {
                FogWindowTexel *texel =
                    scratch.data() +
                    static_cast<std::size_t>(chunkRow * kFieldChunkEdge + y) * rowTexels +
                    static_cast<std::size_t>(chunkColumn * kFieldChunkEdge);
                const std::size_t rowOffset = static_cast<std::size_t>(y) * kFieldChunkEdge;
                const std::uint8_t *cells =
                    fieldChunk == nullptr ? nullptr : fieldChunk->cells().data() + rowOffset;
                const std::uint32_t *transient = transientChunk == nullptr
                                                     ? nullptr
                                                     : transientChunk->cells().data() + rowOffset;
                const std::uint32_t *masks =
                    maskChunk == nullptr ? nullptr : maskChunk->cells().data() + rowOffset;
                for (int x = 0; x < kFieldChunkEdge; ++x) {
                    const std::uint32_t mask = masks == nullptr
                                                   ? IRComponents::kFogChannelDefault
                                                   : (masks[x] ^ IRComponents::kFogChannelDefault);
                    std::uint32_t state =
                        cells == nullptr ? IRComponents::kFogStateUnexplored : cells[x];
                    if (transient != nullptr && (transient[x] & mask) != 0u) {
                        state = IRComponents::kFogStateVisible;
                    }
                    texel[x] = FogWindowTexel{state, mask};
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
    std::vector<FogWindowTexel> upload_;
};

/// One frame of the window gather, GPU-free: drains the field's pending set,
/// plans the window at @p origin against @p windowOrigin (the origin the
/// texture currently shows, updated here), expands each planned rectangle and
/// hands it to @p upload as `(rect, texels)`, then, on a frame whose origin
/// changed, evicts every region outside the window's field-chunk rectangle
/// grown by `kFogResidentMarginChunks`. The eviction runs after the expansion
/// so an in-window region is probed once per residency epoch, and on the
/// first frame too, which clears the access bits the initial reveals set. A
/// second call in one frame finds nothing pending and issues nothing.
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
        const std::size_t texels =
            static_cast<std::size_t>(rect.size_.x) * static_cast<std::size_t>(rect.size_.y);
        if (scratch.upload_.size() < texels) {
            scratch.upload_.resize(texels);
        }
        expandWindowChunks(field, origin, edge, rect, scratch.upload_);
        upload(rect, std::span<const FogWindowTexel>{scratch.upload_.data(), texels});
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

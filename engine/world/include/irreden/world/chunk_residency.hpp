#ifndef IRREDEN_WORLD_CHUNK_RESIDENCY_H
#define IRREDEN_WORLD_CHUNK_RESIDENCY_H

// Chunk-residency manager (Epic E). Owns the sparse map from chunk-coord
// to per-chunk residency slot, the per-chunk voxel sub-pool, and the
// per-chunk entity manifest. Designed in
// docs/design/world-streaming.md §"Topic 2 — Residency manager API".
//
// E1 added the data model + synchronous request/evict + entity ownership +
// per-chunk voxel sub-pool from an injected allocator. E6 (this slice)
// adds optional disk persistence — when a `ChunkDiskPersistence` pointer
// is wired in Config, `requestResident` first attempts to load the chunk
// from disk and seed the pool slice, and `requestEvict` saves dirty
// chunks before dropping the slot. Async upload pipeline, eviction
// policy, and the residency worker pool are still deferred to E2/E3.
//
// Single-chunk creations stay zero-overhead — the manager is only
// constructed when a creation opts into streaming.

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/render/voxel_pool_allocation.hpp>
#include <irreden/world/chunk_coord.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace IRWorld {

class ChunkDiskPersistence;

/// What does the caller want this request urgency-classed as?
/// VISIBLE_RENDER and PREFETCH_RING are camera-derived; FORCED is the
/// editor "load now" path that bypasses any future budget gate.
enum class RequestPriority : std::uint8_t {
    VISIBLE_RENDER,
    PREFETCH_RING,
    FORCED,
};

/// Phase a chunk is in within its residency lifecycle. The E1 skeleton
/// transitions LOADING → UPLOADING → RESIDENT synchronously inside
/// requestResident; the async pipeline lands in E3.
enum class ChunkResidencyState : std::uint8_t {
    LOADING,
    UPLOADING,
    RESIDENT,
    EVICTING,
};

/// Per-chunk residency record. Tracks the chunk's voxel sub-pool, the
/// entities owned by the chunk, and the bookkeeping the eviction policy
/// will read in E2+.
struct ChunkResidencySlot {
    IRPrefab::Chunk::ChunkKey key_ = 0;
    ChunkResidencyState state_ = ChunkResidencyState::LOADING;

    // Voxel sub-pool slice for this chunk. Empty span when the manager
    // has no pool allocator wired (tests, headless smoke).
    IRRender::VoxelPoolAllocation poolAllocation_{};

    // Entities the chunk owns. Append on attach/migrate-in; erase on
    // detach/migrate-out. Ordering is not load-bearing.
    std::vector<IREntity::EntityId> ownedEntities_{};

    // For E2 eviction policy.
    float distanceVoxels_ = 0.0f;
    std::uint64_t lastTouchedFrame_ = 0;

    /// True iff the chunk has been mutated since the last successful
    /// save (or since allocation, for fresh slots). Consulted at
    /// EVICTING → EVICTED by E6's save path. Set only by
    /// `ChunkResidencyManager::markChunkDirty` — direct field writes
    /// are not supported; see engine/world/CLAUDE.md "Chunk mutation
    /// must route through markChunkDirty" for the contract.
    bool isDirty() const noexcept {
        return m_dirty;
    }

  private:
    friend class ChunkResidencyManager;
    bool m_dirty = false;
};

/// Owns the sparse resident-set; the engine-side API for chunk identity
/// + per-chunk voxel sub-pool allocation + entity ownership. Constructed
/// only by creations that opt into streaming — single-chunk creations
/// never see this class.
class ChunkResidencyManager {
  public:
    /// Allocator callback that draws a voxel sub-pool slice from the
    /// global pool. Production wires this to IRRender::allocateVoxels;
    /// tests can stub it to keep the manager testable without a real
    /// RenderManager / GPU context.
    using PoolAllocator = std::function<IRRender::VoxelPoolAllocation(unsigned int)>;

    struct Config {
        /// Optional allocator. When unset, slots receive an empty
        /// allocation (skeleton/test mode).
        PoolAllocator poolAllocator_{};

        /// Voxel count requested from the allocator per resident chunk.
        /// 0 → skip allocation even when an allocator is wired. Sized
        /// from the disk record in the eventual streaming path; the
        /// E1 skeleton uses one number for every chunk.
        unsigned int voxelsPerChunk_ = 0;

        /// Optional disk-persistence sink. When set:
        /// - `requestResident` first tries `persistence_->loadChunk(key)`
        ///   and, if the file exists and the record count matches the
        ///   pool slice, seeds the slice from disk. Missing files leave
        ///   the slice in its default-allocated state (fresh chunk).
        /// - `requestEvict` saves dirty chunks before dropping the slot,
        ///   matching the design's "snapshot-at-schedule-time" rule
        ///   (today's synchronous path makes the snapshot implicit —
        ///   we save then erase in the same call).
        /// Caller owns the persistence object; the manager only borrows.
        ChunkDiskPersistence *persistence_ = nullptr;
    };

    ChunkResidencyManager() = default;
    explicit ChunkResidencyManager(Config config);
    ~ChunkResidencyManager() = default;

    ChunkResidencyManager(const ChunkResidencyManager &) = delete;
    ChunkResidencyManager &operator=(const ChunkResidencyManager &) = delete;
    ChunkResidencyManager(ChunkResidencyManager &&) = default;
    ChunkResidencyManager &operator=(ChunkResidencyManager &&) = default;

    // ── Frame hooks (stubs in E1; populated by E2/E3) ────────────────

    void beginFrame();
    void tickPrefetch();
    void flushUploads(int maxBytes);
    void endFrame();

    // ── Synchronous slot API ─────────────────────────────────────────

    /// True when the chunk has a slot AND that slot has reached
    /// RESIDENT. LOADING / UPLOADING / EVICTING chunks return false so
    /// callers don't read a half-attached voxel pool.
    bool isResident(IRPrefab::Chunk::ChunkKey key) const;

    /// Pointer-or-nullptr accessor. The returned pointer is valid until
    /// the next mutation of the resident set (any call to requestResident
    /// or requestEvict, on this key or any other) — unordered_map may
    /// rehash on insert, invalidating all references. Do not cache the
    /// pointer across mutating calls.
    const ChunkResidencySlot *slot(IRPrefab::Chunk::ChunkKey key) const;
    ChunkResidencySlot *slot(IRPrefab::Chunk::ChunkKey key);

    /// Add the chunk to the resident set if it isn't there already.
    /// In the E1 skeleton the slot transitions LOADING → UPLOADING →
    /// RESIDENT inline; the async lift lives in E3. The pool allocator
    /// (if any) is called on first request; re-requesting a resident
    /// slot just bumps lastTouchedFrame_.
    void requestResident(IRPrefab::Chunk::ChunkKey key, RequestPriority priority);

    /// Drop the chunk from the resident set. When `Config::persistence_`
    /// is wired and the slot's dirty bit is set, the chunk is saved
    /// to disk before erasure. Pool allocation is currently leaked back
    /// to the global pool's free-list when the allocator supports it
    /// (today's RenderManager pool is bump-style — see
    /// engine/render/CLAUDE.md). E2 introduces the dealloc path.
    void requestEvict(IRPrefab::Chunk::ChunkKey key);

    /// Force-save every resident slot whose dirty bit is set
    /// (design's "save-all path"). On success the slot's dirty bit
    /// clears so subsequent eviction skips the redundant save. Slots
    /// stay resident; only the on-disk copy is refreshed.
    ///
    /// Synchronous in this v1 — the editor's save-snapshot button
    /// calls this and blocks until all per-chunk writes finish. E3's
    /// worker pool turns each per-chunk save into a queued job; the
    /// surface stays the same (the method still blocks until the
    /// queue drains).
    void flushPendingSaves();

    /// Mark @p key's slot dirty so eviction (or `flushPendingSaves`)
    /// will write its voxel slice to disk. **This is the only
    /// supported way to flip the dirty bit** — `ChunkResidencySlot`'s
    /// underlying field is private. Any system that writes to a
    /// chunk-owned `VoxelPoolAllocation` must call this immediately
    /// after the write; see engine/world/CLAUDE.md "Chunk mutation
    /// must route through markChunkDirty" for the contract and the
    /// rationale (without it a missed `dirty` flip silently drops
    /// the save on eviction and the chunk reverts on re-resident).
    ///
    /// No-op when @p key has no slot at all — the manager logs nothing
    /// because the eventual streaming path will sometimes target a
    /// chunk that already evicted between request and write; callers
    /// that need stronger guarantees should check `isResident(key)`
    /// before mutating. Note: a slot in LOADING or UPLOADING state
    /// (not yet resident) will still be marked dirty by this call.
    void markChunkDirty(IRPrefab::Chunk::ChunkKey key);

    // ── Entity ownership ─────────────────────────────────────────────

    /// Append an entity to the chunk's owned list. Caller is responsible
    /// for keeping the entity's C_ChunkMembership.chunkCoord_ in sync.
    /// No-op if the chunk has no slot.
    void attachEntity(IREntity::EntityId id, IRPrefab::Chunk::ChunkKey key);

    /// Move an entity from one chunk's ownership to another. If
    /// destination is non-resident, the destination slot is force-
    /// requested first (matches the design doc edge case 1; the
    /// pending-migration queue is deferred to E4).
    void migrateEntity(
        IREntity::EntityId id, IRPrefab::Chunk::ChunkKey oldKey, IRPrefab::Chunk::ChunkKey newKey
    );

    // ── Index iteration (acceptance criterion 1) ─────────────────────

    std::size_t residentChunkCount() const;
    std::size_t entityCount() const;

    /// Iterate every resident slot. Visit order is unordered_map's;
    /// callers must not rely on it.
    template <typename Fn> void forEachChunk(Fn &&fn) const {
        // TODO(E3): filter to state_ == RESIDENT once async transitions add LOADING/UPLOADING slots
        for (const auto &kv : m_slots) {
            fn(kv.first, kv.second);
        }
    }

  private:
    Config m_config{};
    std::unordered_map<IRPrefab::Chunk::ChunkKey, ChunkResidencySlot> m_slots{};
    std::uint64_t m_frameIndex = 0;
};

} // namespace IRWorld

#endif /* IRREDEN_WORLD_CHUNK_RESIDENCY_H */

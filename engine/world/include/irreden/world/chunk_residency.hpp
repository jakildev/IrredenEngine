#ifndef IRREDEN_WORLD_CHUNK_RESIDENCY_H
#define IRREDEN_WORLD_CHUNK_RESIDENCY_H

// Chunk-residency manager (Epic E). Owns the sparse map from chunk-coord
// to per-chunk residency slot, the per-chunk voxel sub-pool, and the
// per-chunk entity manifest. Designed in
// docs/design/world-streaming.md §"Topic 2 — Residency manager API".
//
// E1 added the data model + synchronous request/evict + entity ownership +
// per-chunk voxel sub-pool from an injected allocator. E6 added optional
// disk persistence. E2 (this slice) adds the camera-driven eviction
// policy: distance-based bucketing with LRU tie-breaking, a budget cap
// on max resident chunks, pool deallocation on eviction, and the
// per-frame beginFrame/endFrame lifecycle that drives the eviction cycle.
// The async upload pipeline and worker threads are deferred to E3.
//
// Single-chunk creations stay zero-overhead — the manager is only
// constructed when a creation opts into streaming.

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/ir_math.hpp>
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
/// reads each frame.
struct ChunkResidencySlot {
    IRPrefab::Chunk::ChunkKey key_ = 0;
    ChunkResidencyState state_ = ChunkResidencyState::LOADING;

    IRRender::VoxelPoolAllocation poolAllocation_{};

    std::vector<IREntity::EntityId> ownedEntities_{};

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
    using PoolAllocator = std::function<IRRender::VoxelPoolAllocation(unsigned int)>;
    using PoolDeallocator = std::function<void(const IRRender::VoxelPoolAllocation &)>;

    struct Config {
        PoolAllocator poolAllocator_{};
        PoolDeallocator poolDeallocator_{};

        unsigned int voxelsPerChunk_ = 0;

        ChunkDiskPersistence *persistence_ = nullptr;

        /// Budget: max chunks that may be RESIDENT simultaneously.
        /// When the set exceeds this cap, endFrame evicts the
        /// furthest + oldest-LRU slots until back in budget.
        unsigned int maxResidentChunks_ = 256;

        /// R_view: chunks within this radius (in voxels from camera)
        /// must be RESIDENT or UPLOADING. Default matches the
        /// light-volume window (128 voxels = 4 chunks at kChunkEdge=32).
        float viewRadiusVoxels_ = 128.0f;

        /// R_prefetch: chunks within this radius are eligible for
        /// loading if budget permits.
        float prefetchRadiusVoxels_ = 256.0f;

        /// Hysteresis margin (in voxels) to prevent thrashing at the
        /// eviction boundary. A chunk must exceed
        /// R_prefetch + hysteresisVoxels_ to be marked EVICTING.
        float hysteresisVoxels_ = 32.0f;

        /// Chebyshev radius (in chunks) for the prefetch ring around
        /// the camera. tickPrefetch requests residency for every chunk
        /// within this radius. 0 disables prefetch. Eviction is driven
        /// by the voxel-distance policy above (beginFrame / endFrame),
        /// not by this radius.
        int prefetchRadiusChunks_ = 2;
    };

    ChunkResidencyManager() = default;
    explicit ChunkResidencyManager(Config config);
    ~ChunkResidencyManager() = default;

    ChunkResidencyManager(const ChunkResidencyManager &) = delete;
    ChunkResidencyManager &operator=(const ChunkResidencyManager &) = delete;
    ChunkResidencyManager(ChunkResidencyManager &&) = default;
    ChunkResidencyManager &operator=(ChunkResidencyManager &&) = default;

    // ── Frame hooks ────────────────────────────────────────────────────

    /// Recompute per-slot distances from the camera, derive the chunk
    /// coordinate for the prefetch ring, bump touch frames for slots
    /// within R_view, and mark far slots EVICTING.
    void beginFrame(IRMath::vec3 cameraWorldVoxel);

    void tickPrefetch();
    void flushUploads(int maxBytes);

    /// Process evictions: save dirty EVICTING slots, deallocate pool
    /// slices, and enforce the budget cap.
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

    /// Drop the chunk from the resident set. Saves dirty chunks via
    /// persistence (if wired), calls PoolDeallocator to return the
    /// pool slice, and erases the slot.
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

    struct FrameStats {
        unsigned int evictedThisFrame_ = 0;
        unsigned int loadedThisFrame_ = 0;
        unsigned int residentCount_ = 0;
    };
    const FrameStats &frameStats() const {
        return m_frameStats;
    }

    /// Iterate every resident slot. Visit order is unordered_map's;
    /// callers must not rely on it.
    template <typename Fn> void forEachChunk(Fn &&fn) const {
        // TODO(E3): filter to state_ == RESIDENT once async transitions add LOADING/UPLOADING slots
        for (const auto &kv : m_slots) {
            fn(kv.first, kv.second);
        }
    }

  private:
    void evictSlot(std::unordered_map<IRPrefab::Chunk::ChunkKey, ChunkResidencySlot>::iterator it);

    Config m_config{};
    std::unordered_map<IRPrefab::Chunk::ChunkKey, ChunkResidencySlot> m_slots{};
    std::uint64_t m_frameIndex = 0;
    IRMath::vec3 m_cameraWorldVoxel{0.0f};
    IRMath::ivec3 m_cameraChunk{0};
    FrameStats m_frameStats{};
};

} // namespace IRWorld

#endif /* IRREDEN_WORLD_CHUNK_RESIDENCY_H */

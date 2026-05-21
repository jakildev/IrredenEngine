#ifndef IRREDEN_WORLD_CHUNK_RESIDENCY_H
#define IRREDEN_WORLD_CHUNK_RESIDENCY_H

// Chunk-residency manager skeleton (Epic E / E1). Owns the sparse map
// from chunk-coord to per-chunk residency slot, the per-chunk voxel sub-
// pool, and the per-chunk entity manifest. Designed in
// docs/design/world-streaming.md §"Topic 2 — Residency manager API".
//
// E1 scope: data model + synchronous request/evict + entity ownership +
// per-chunk voxel sub-pool from an injected allocator. Async upload
// pipeline, eviction policy, prefetch ring, and dirty-tracking save
// path are deferred to E2/E3/E6.
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

    // Set by any voxel/entity mutation; consulted at EVICTING → EVICTED
    // by E6's save path. E1 leaves the writer hooks for E4/E6.
    bool dirty_ = false;
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

    /// Drop the chunk from the resident set. Pool allocation is currently
    /// leaked back to the global pool's free-list when the allocator
    /// supports it (today's RenderManager pool is bump-style — see
    /// engine/render/CLAUDE.md). E2 introduces the dealloc path.
    void requestEvict(IRPrefab::Chunk::ChunkKey key);

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

#ifndef IRREDEN_WORLD_CHUNK_RESIDENCY_H
#define IRREDEN_WORLD_CHUNK_RESIDENCY_H

// Chunk-residency manager (Epic E). Owns the sparse map from chunk-coord
// to per-chunk residency slot, the per-chunk voxel sub-pool, and the
// per-chunk entity manifest. Designed in
// docs/design/world-streaming.md §"Topic 2 — Residency manager API"
// and §"Topic 4 — Upload-bandwidth cap + low-LOD fallback".
//
// E1 added the data model + synchronous request/evict + entity ownership +
// per-chunk voxel sub-pool from an injected allocator. E6 added optional
// disk persistence — when a `ChunkDiskPersistence` pointer is wired in
// Config, `requestResident` first attempts to load the chunk from disk
// and seed the pool slice, and `requestEvict` saves dirty chunks before
// dropping the slot. E3 (Chebyshev prefetch + camera-radius eviction) is
// wired into `tickPrefetch()`.
//
// T-358 (Topic 4 / "E4" in issue numbering) adds the per-frame upload-
// bandwidth cap and low-LOD billboard metadata. Opt in via
// `Config::deferredUpload_ = true`: `requestResident` enqueues the chunk
// in LOADING and `flushUploads(maxBytes)` drains the queue each frame in
// (priority, distance) order capped at `maxBytes`. Chunks not yet drained
// stay in LOADING/UPLOADING; the renderer reads `forEachLowLodSlot` to
// spawn AABB billboards from `aabbColor_` / `aabbMinVoxel_` /
// `aabbMaxVoxel_` until the upload completes. The async upload-worker
// pool described in the design's Topic 2 is deferred to a follow-up —
// today's "upload" is the synchronous E1 allocate + disk-load + transition
// to RESIDENT, just gated by the per-frame byte budget.
//
// Single-chunk creations stay zero-overhead — the manager is only
// constructed when a creation opts into streaming.

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/voxel_pool_allocation.hpp>
#include <irreden/world/chunk_coord.hpp>

#include <cstddef>
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

    // ── Low-LOD billboard metadata (T-358) ──────────────────────────
    // Read by the renderer's low-LOD pass when the slot has not yet
    // reached RESIDENT. Defaults represent the slot as a full-chunk
    // grey AABB — the design doc's "this content is not yet uploaded"
    // visual signal (Topic 4: "a flat cube where a complex structure
    // should be is an unambiguous visual signal that streaming is
    // mid-flight"). On-disk BBOX chunk records (design Topic 6) will
    // override these defaults once the .vxs format extension lands.
    /// Mean RGBA color of the chunk's voxel content. Default grey
    /// `(0xAA, 0xAA, 0xAA, 0xFF)`. After the chunk reaches RESIDENT
    /// the renderer drops the low-LOD billboard; this field is then
    /// only relevant for re-eviction → low-LOD cycles.
    IRMath::Color aabbColor_{0xAA, 0xAA, 0xAA, 0xFF};
    /// AABB minimum corner in chunk-local voxel coords. Default
    /// `{0,0,0}` — covers the full chunk.
    IRMath::ivec3 aabbMinVoxel_{0, 0, 0};
    /// AABB maximum corner in chunk-local voxel coords. Default
    /// covers the full chunk: `kChunkSize - 1`.
    IRMath::ivec3 aabbMaxVoxel_{
        static_cast<int>(IRConstants::kChunkSize.x) - 1,
        static_cast<int>(IRConstants::kChunkSize.y) - 1,
        static_cast<int>(IRConstants::kChunkSize.z) - 1,
    };
    /// Bit 0: blocks light. Bit 1: emissive proxy. Future bits per
    /// design Topic 6. Default 0 (passive AABB billboard, no special
    /// lighting interaction).
    std::uint8_t lowLodFlags_ = 0;

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

        /// Chebyshev radius (in chunks) beyond which resident chunks
        /// are evicted. Must be > prefetchRadiusChunks_ to provide
        /// hysteresis (avoids thrashing at the ring boundary).
        int evictionRadiusChunks_ = 3;

        // ── Per-frame upload-bandwidth cap (T-358) ──────────────────
        // Opt-in to async/budget upload semantics. When false
        // (default), requestResident keeps E1's synchronous behavior:
        // the chunk reaches RESIDENT inside the call. When true,
        // requestResident enqueues the chunk in LOADING; the slot
        // does NOT reach RESIDENT until flushUploads(maxBytes)
        // processes it. The design's "load-bearing invariant: no
        // frame ever blocks on upload" hinges on this gate.
        bool deferredUpload_ = false;

        // Default per-frame upload byte budget. Passed implicitly to
        // flushUploads(0) — callers wire World::gameLoop() to either
        // pass 0 (use this default) or an explicit override. Design
        // doc Topic 4 picks 4 MiB / frame ≈ 240 MiB/s @ 60 fps so the
        // GPU upload queue never blocks render dispatches. Tunable per
        // creation; only consulted when `deferredUpload_ == true`.
        int defaultUploadBudgetBytes_ = 4 * 1024 * 1024;
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

    /// Drain the pending-uploads queue up to @p maxBytes, in
    /// (priority, distance) order. Chunks reach RESIDENT inside this
    /// call; chunks deferred to the next frame stay in LOADING and
    /// the renderer paints them as low-LOD AABB billboards via
    /// `forEachLowLodSlot`.
    ///
    /// Special values:
    /// - `maxBytes == 0`: use `Config::defaultUploadBudgetBytes_`.
    /// - `maxBytes < 0`: treated as 0 (defaults; never as "unlimited"
    ///   to avoid silent budget-bypass).
    ///
    /// FORCED-priority pending entries bypass the budget — the
    /// editor's "load this chunk now" requires synchronous completion
    /// even when budget is exhausted.
    ///
    /// Single-chunk-exceeds-budget guard: at least one non-forced
    /// chunk completes per call even if its byte size > the budget,
    /// otherwise streaming stalls forever when one chunk is bigger
    /// than the cap. Bias is intentional: bumping over budget once is
    /// better than never making progress.
    ///
    /// No-op when `Config::deferredUpload_` is false (no queue exists
    /// in synchronous mode; chunks reach RESIDENT inside
    /// requestResident).
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

    /// Total number of tracked chunks in m_slots, including LOADING,
    /// UPLOADING, and EVICTING slots in deferred mode. Not strictly
    /// "resident" when deferredUpload_ is true.
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

    /// Count of chunks waiting in the deferred-upload queue. Always 0
    /// when `Config::deferredUpload_` is false. Exposed for tests and
    /// for budget-tuning telemetry.
    std::size_t pendingUploadCount() const;

    /// Iterate every tracked slot (all states). Visit order is
    /// unordered_map's; callers must not rely on it.
    /// TODO(T-358-follow-up): filter to state_ == RESIDENT once a
    /// production consumer exists that must skip LOADING slots.
    template <typename Fn> void forEachChunk(Fn &&fn) const {
        for (const auto &kv : m_slots) {
            fn(kv.first, kv.second);
        }
    }

    /// Iterate every slot that has NOT yet reached RESIDENT (LOADING,
    /// UPLOADING, or EVICTING). The renderer's low-LOD pass calls this
    /// each frame to spawn / refresh AABB billboards from each slot's
    /// `aabbColor_` / `aabbMinVoxel_` / `aabbMaxVoxel_` while the
    /// upload pipeline catches up. Visit order is unordered_map's.
    template <typename Fn> void forEachLowLodSlot(Fn &&fn) const {
        for (const auto &kv : m_slots) {
            if (kv.second.state_ != ChunkResidencyState::RESIDENT) {
                fn(kv.first, kv.second);
            }
        }
    }

  private:
    void evictSlot(std::unordered_map<IRPrefab::Chunk::ChunkKey, ChunkResidencySlot>::iterator it);

    // Pending-upload queue entry. Populated by requestResident when
    // Config::deferredUpload_ is true; drained by flushUploads in
    // (priority, distance) order. The slot itself lives in m_slots
    // throughout — the queue just orders the work, not the data.
    struct PendingUpload {
        IRPrefab::Chunk::ChunkKey key_ = 0;
        RequestPriority priority_ = RequestPriority::PREFETCH_RING;
        std::uint64_t enqueueFrame_ = 0;
    };

    // Shared by both the synchronous-mode requestResident path and the
    // async-mode flushUploads drain — does the actual allocate + disk
    // load + transition-to-RESIDENT work for a single slot.
    void completeUploadForSlot(ChunkResidencySlot &s);

    // Update the pending-upload entry's priority to the more urgent of
    // (existing, requested). No-op if the key isn't queued.
    void bumpPendingUploadPriority(IRPrefab::Chunk::ChunkKey key, RequestPriority p);

    Config m_config{};
    std::unordered_map<IRPrefab::Chunk::ChunkKey, ChunkResidencySlot> m_slots{};
    std::vector<PendingUpload> m_pendingUploads{};
    std::uint64_t m_frameIndex = 0;
    IRMath::vec3 m_cameraWorldVoxel{0.0f};
    IRMath::ivec3 m_cameraChunk{0};
    FrameStats m_frameStats{};

    // Reused across flushUploads() calls to avoid per-frame heap allocation.
    std::vector<PendingUpload> m_deferScratch{};
};

} // namespace IRWorld

#endif /* IRREDEN_WORLD_CHUNK_RESIDENCY_H */

#ifndef COMPONENT_VOXEL_POOL_H
#define COMPONENT_VOXEL_POOL_H

#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/voxel_pool_allocation.hpp>

#include <irreden/voxel/components/component_voxel.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <optional>
#include <map>
#include <limits>

using namespace IRMath;
using IREntity::EntityId;

namespace IRComponents {

// Each mask word covers `kVoxelActiveMaskBits` consecutive voxel slots.
// CPU-side storage is `std::vector<uint32_t>`; the GPU compact shader
// reads the same memory through a `uint activeMask[]` SSBO at
// `kBufferIndex_VoxelActiveMask`.
constexpr std::size_t kVoxelActiveMaskBits = 32;

struct ChunkBounds {
    vec2 isoMin_ = vec2(std::numeric_limits<float>::max());
    vec2 isoMax_ = vec2(std::numeric_limits<float>::lowest());
    // Nearest raw iso depth (x+y+z = pos3DtoDistance) of the chunk's live
    // voxels, in the same projection as isoMin_/isoMax_. Consumed by the
    // chunk-occlusion pre-pass (#1294 child 2/3) as the chunk's front-most
    // depth for the Hi-Z compare. Only the cardinal `rebuildChunkBounds` path
    // fills it (the only path the cull is enabled on); other paths leave the
    // sentinel and their chunks are flagged not cull-eligible.
    float minDepth_ = std::numeric_limits<float>::max();

    void expand(vec2 isoPos) {
        isoMin_ = IRMath::min(isoMin_, isoPos);
        isoMax_ = IRMath::max(isoMax_, isoPos);
    }

    void reset() {
        isoMin_ = vec2(std::numeric_limits<float>::max());
        isoMax_ = vec2(std::numeric_limits<float>::lowest());
        minDepth_ = std::numeric_limits<float>::max();
    }
};

// Static (camera-independent) world-space AABB of a chunk's live voxels.
// Cached once per allocation/position change so the continuous-yaw cull can
// recover a chunk's iso bounds by projecting 8 corners under the live yaw
// (`IRMath::isoAABBOfWorldAABBUnderYaw`) — O(chunks)/frame — instead of
// re-projecting every voxel every frame (#1439).
struct ChunkWorldBounds {
    vec3 worldMin_ = vec3(std::numeric_limits<float>::max());
    vec3 worldMax_ = vec3(std::numeric_limits<float>::lowest());

    void expand(vec3 worldPos) {
        worldMin_ = IRMath::min(worldMin_, worldPos);
        worldMax_ = IRMath::max(worldMax_, worldPos);
    }

    // A chunk with no live voxels keeps the inverted sentinel.
    bool empty() const {
        return worldMin_.x > worldMax_.x;
    }
};

struct C_VoxelPool {
  public:
    // Cardinal store tie-possibility signal (#2346). TRUE when the single-
    // canvas store's (iso pixel, encoded depth) key may stop being a bijection
    // of this pool's live voxels — any live voxel off the integer lattice
    // (|pos − round(pos)| > the snapNearIntegerVoxelPosition epsilon), any two
    // live voxels sharing a roundHalfUp cell, or any GPU-transformed slot
    // (its world position is authored GPU-side and generically fractional).
    // Recomputed by VOXEL_TO_TRIXEL_STAGE_1 on frames whose CPU position
    // upload changed binding 5 (see recomputeStoreTiesPossible there) — static
    // scenes scan once at seed. When set, the winner-election dispatch + the
    // winner-guarded stage 2 replace the default cardinal stage 2 so the
    // colour/entity-id planes stay deterministic among equal-key faces;
    // lattice pools keep exactly today's programs and dispatch count.
    bool storeTiesPossible_ = false;

    C_VoxelPool(ivec3 numVoxels)
        : m_voxelPoolSize{numVoxels.x * numVoxels.y * numVoxels.z}
        , m_voxelPoolSize3D{numVoxels}
        , m_voxelPositions{}
        , m_voxelPositionsOffset{}
        , m_voxelPositionsGlobal{}
        , m_voxelColors{}
        , m_entityIdsDirty{true}
        , m_chunkBoundsDirty{true} {

        m_voxelPositions.resize(m_voxelPoolSize);
        std::fill(
            m_voxelPositions.begin(),
            m_voxelPositions.end(),
            IRRender::VoxelGpuPosition{vec3(0, 0, 0), 0.0f}
        );

        m_voxelPositionsOffset.resize(m_voxelPoolSize);
        std::fill(m_voxelPositionsOffset.begin(), m_voxelPositionsOffset.end(), vec3(0, 0, 0));

        m_voxelPositionsGlobal.resize(m_voxelPoolSize);
        std::fill(
            m_voxelPositionsGlobal.begin(),
            m_voxelPositionsGlobal.end(),
            IRRender::VoxelGpuPosition{vec3(0, 0, 0), 0.0f}
        );

        m_voxelColors.resize(m_voxelPoolSize);
        std::fill(m_voxelColors.begin(), m_voxelColors.end(), C_Voxel{Color{0, 0, 0, 0}});

        m_voxelEntities.resize(m_voxelPoolSize);
        std::fill(m_voxelEntities.begin(), m_voxelEntities.end(), IREntity::kNullEntity);

        // Every voxel starts CPU-direct (static); a voxel set opts into GPU
        // transform indirection by pointing its range at an EntityTransformBuffer
        // slot via `setTransformIndexForRange`.
        m_voxelTransformIndices.resize(m_voxelPoolSize);
        std::fill(
            m_voxelTransformIndices.begin(),
            m_voxelTransformIndices.end(),
            IRRender::kVoxelTransformStatic
        );

        const std::size_t maskWords =
            (static_cast<std::size_t>(m_voxelPoolSize) + kVoxelActiveMaskBits - 1) /
            kVoxelActiveMaskBits;
        m_activeMask.assign(maskWords, 0u);
    }

    C_VoxelPool() {}

    // EntityId addVoxel

    IRRender::VoxelPoolAllocation allocateVoxels(unsigned int size) {
        auto freeSpan = findFreeSpan(size);
        if (freeSpan.has_value()) {
            size_t startIndex = freeSpan->first;
            IRE_LOG_DEBUG("Reusing existing span from {} to {}", startIndex, startIndex + size - 1);
            m_freeSpanLookup[size].erase(*freeSpan);
            if (m_freeSpanLookup[size].empty()) {
                m_freeSpanLookup.erase(size);
            }
            markCullBoundsDirty(startIndex, size);
            return IRRender::VoxelPoolAllocation{
                startIndex,
                std::span<IRRender::VoxelGpuPosition>{m_voxelPositions.data() + startIndex, size},
                std::span<vec3>{m_voxelPositionsOffset.data() + startIndex, size},
                std::span<IRRender::VoxelGpuPosition>{
                    m_voxelPositionsGlobal.data() + startIndex,
                    size
                },
                std::span<C_Voxel>{m_voxelColors.data() + startIndex, size}
            };
        }

        if (m_voxelPoolIndex + size <= m_voxelPoolSize) {
            size_t startIndex = static_cast<size_t>(m_voxelPoolIndex);
            m_voxelPoolIndex += size;
            markCullBoundsDirty(startIndex, size);
            IRE_LOG_DEBUG("Allocated voxels from {} to {}", startIndex, m_voxelPoolIndex - 1);
            return IRRender::VoxelPoolAllocation{
                startIndex,
                std::span<IRRender::VoxelGpuPosition>{m_voxelPositions.data() + startIndex, size},
                std::span<vec3>{m_voxelPositionsOffset.data() + startIndex, size},
                std::span<IRRender::VoxelGpuPosition>{
                    m_voxelPositionsGlobal.data() + startIndex,
                    size
                },
                std::span<C_Voxel>{m_voxelColors.data() + startIndex, size}
            };
        }

        IR_ASSERT(false, "Ran out of voxels");

        return IRRender::VoxelPoolAllocation{
            0,
            std::span<IRRender::VoxelGpuPosition>{},
            std::span<vec3>{},
            std::span<IRRender::VoxelGpuPosition>{},
            std::span<C_Voxel>{}
        };
    }

    void deallocateVoxels(size_t startIndex, size_t size) {
        IR_ASSERT(
            startIndex + size <= static_cast<size_t>(m_voxelPoolSize),
            "deallocateVoxels out of bounds: startIndex={}, size={}, poolSize={}",
            startIndex,
            size,
            m_voxelPoolSize
        );
        for (size_t i = 0; i < size; i++) {
            m_voxelColors[startIndex + i].color_ = Color{0, 0, 0, 0};
        }
        clearActiveMaskRange(startIndex, size);

        std::fill(
            m_voxelEntities.begin() + startIndex,
            m_voxelEntities.begin() + startIndex + size,
            IREntity::kNullEntity
        );
        m_entityIdsDirty = true;
        markCullBoundsDirty(startIndex, size);

        m_freeVoxelSpans.push_back({startIndex, size});
        updateFreeSpanLookup(startIndex, size);
    }

    std::vector<IRRender::VoxelGpuPosition> &getPositions() {
        return m_voxelPositions;
    }
    std::vector<vec3> &getPositionOffsets() {
        return m_voxelPositionsOffset;
    }
    std::vector<IRRender::VoxelGpuPosition> &getPositionGlobals() {
        return m_voxelPositionsGlobal;
    }
    std::vector<C_Voxel> &getColors() {
        return m_voxelColors;
    }

    const std::vector<IRRender::VoxelGpuPosition> &getPositions() const {
        return m_voxelPositions;
    }

    const std::vector<vec3> &getPositionOffsets() const {
        return m_voxelPositionsOffset;
    }

    const std::vector<IRRender::VoxelGpuPosition> &getPositionGlobals() const {
        return m_voxelPositionsGlobal;
    }

    const std::vector<C_Voxel> &getColors() const {
        return m_voxelColors;
    }

    int getVoxelPoolSize() const {
        return m_voxelPoolSize;
    }
    int getLiveVoxelCount() const {
        return m_voxelPoolIndex;
    }
    ivec3 getVoxelPoolSize3D() const {
        return m_voxelPoolSize3D;
    }

    // Per-trixel-priority aggregate (#2155). Maintained push-at-mutation by the
    // C_VoxelSetNew priority mutators (changeVoxelPriority / changeVoxelPriorityAll
    // adjust by their delta; onDestroy releases the set's contribution) — never a
    // per-voxel scan. Read once per frame by VOXEL_TO_TRIXEL_STAGE_1 to stamp the
    // canvas's C_TriangleCanvasTextures::anyPerTrixelPriority_, which gates the
    // finalization shader's entity-id decode read. Clamped at 0 so a stray
    // over-decrement can't wrap negative (conservative-TRUE keeps correctness
    // regardless; the clamp just protects the FALSE fast-path signal).
    void adjustPerTrixelPriorityVoxelCount(int delta) {
        m_perTrixelPriorityVoxelCount = IRMath::max(0, m_perTrixelPriorityVoxelCount + delta);
    }
    bool hasPerTrixelPriority() const {
        return m_perTrixelPriorityVoxelCount > 0;
    }

    void setEntityIdForRange(size_t startIdx, size_t count, EntityId entityId) {
        IR_ASSERT(
            startIdx + count <= m_voxelEntities.size(),
            "setEntityIdForRange out of bounds: startIdx={}, count={}, poolSize={}",
            startIdx,
            count,
            m_voxelEntities.size()
        );
        // The per-trixel priority carrier (#1960) steals the top 2 bits of the
        // stored 64-bit id (IRRender::kEntityIdPriorityShift). A live id that sets
        // them would be decoded as a non-zero priority AND a corrupted picked id —
        // guard the invariant once per set (entity ids are allocation counters that
        // never approach 2^62). Debug-only; compiled out in release.
        IR_ASSERT(
            (entityId >> IRRender::kEntityIdPriorityShift) == 0,
            "entity id {} sets the per-trixel priority carrier bits "
            "(must stay below 2^{})",
            entityId,
            IRRender::kEntityIdPriorityShift
        );
        std::fill(
            m_voxelEntities.begin() + startIdx,
            m_voxelEntities.begin() + startIdx + count,
            entityId
        );
        m_entityIdsDirty = true;
    }

    const std::vector<EntityId> &getEntityIds() const {
        return m_voxelEntities;
    }

    bool isEntityIdsDirty() const {
        return m_entityIdsDirty;
    }

    void clearEntityIdsDirty() {
        m_entityIdsDirty = false;
    }

    [[deprecated(
        "Capture VoxelPoolAllocation::startIndex_ at allocateVoxels call time instead — see "
        "engine/render/CLAUDE.md"
    )]]
    const IRRender::VoxelGpuPosition *getPositionGlobalsBasePtr() const {
        return m_voxelPositionsGlobal.data();
    }

    int getChunkCount() const {
        return (m_voxelPoolIndex + IRRender::kVoxelChunkSize - 1) / IRRender::kVoxelChunkSize;
    }

    std::vector<ChunkBounds> &getChunkBounds() {
        return m_chunkBounds;
    }

    // @p useContinuousYaw (smooth camera Z-yaw, T3 / #1310): while the per-axis
    // canvases are active the framebuffer scatter rasterizes voxels at their
    // CONTINUOUS yawed iso position, so the chunk-visibility gate must project
    // the same way — the cardinal-snapped chunk bounds otherwise drop off-center
    // chunks (the "missing objects / ground during rotation" symptom). The
    // continuous-yaw bounds change every frame, so they bypass the cardinal-
    // index cache and force a fresh recompute on the next cardinal-path call.
    void rebuildChunkBounds(
        CardinalIndex cardinalIndex = CardinalIndex::k0,
        bool useContinuousYaw = false,
        float visualYaw = 0.0f
    ) {
        // Detached re-voxelize pool (#1556): the GPU scatter compute owns
        // binding 5, so the CPU global mirror no longer follows the per-frame
        // rotation. Drive the chunk-visibility gate from the conservative
        // origin-centered world-AABB seeded once at allocation (a sphere that
        // bounds the solid under EVERY rotation) instead of the now-stale mirror.
        // The pool is small and re-rasterized whole, so every chunk gets the same
        // projected iso bound — a per-chunk bound would buy nothing. Bypasses the
        // cardinal cache: the bound is rotation-independent, so it never drifts.
        if (m_staticReVoxelizeBound.has_value()) {
            const int chunkCount = getChunkCount();
            m_chunkBounds.assign(chunkCount, ChunkBounds{});
            // Cell-anchor shift (#2545): the raster projects cell positions
            // half a cell toward the origin under yaw, so the gate's bound
            // translates the same way to keep covering the rendered mass.
            const IsoBounds2D iso = IRMath::isoAABBOfWorldAABBUnderYaw(
                m_staticReVoxelizeBound->worldMin_ - vec3(0.5f),
                m_staticReVoxelizeBound->worldMax_ - vec3(0.5f),
                visualYaw
            );
            for (ChunkBounds &cb : m_chunkBounds) {
                cb.isoMin_ = iso.min_;
                cb.isoMax_ = iso.max_;
            }
            // The static bound OWNS the cardinal answer for every chunk here:
            // this branch re-derives all of them on every call from a bound
            // that reads neither position nor alpha, so nothing is left owing
            // a cardinal recompute. Consuming the bits is load-bearing, not
            // tidiness — allocation arms them (`allocateVoxels` →
            // `markCullBoundsDirty`) and no other path on this branch clears
            // them, so leaving them set makes `isRangeVisible`'s
            // pending ⇒ admit-conservatively gate answer TRUE forever and the
            // UPDATE-side cull stops culling this pool at all (#2830).
            // Only the cardinal bit: `ensureChunkWorldBounds` is the sole
            // consumer that can satisfy the world bit and this branch never
            // runs it, so the world cache keeps owing its work.
            dropPendingChunks(m_pendingCardinalChunks, kCullPendingCardinal);
            return;
        }

        const int chunkCount = getChunkCount();

        if (useContinuousYaw) {
            m_chunkBounds.assign(static_cast<std::size_t>(chunkCount), ChunkBounds{});
            // Closed-form O(chunks) cull region (#1439): project each chunk's
            // cached static world-AABB under the live yaw instead of
            // re-projecting every voxel. The 8-corner projection is a
            // conservative superset of the per-voxel iso bounds
            // (`pos3DtoPos2DIsoYawed` is linear), so the gate over-includes
            // rather than dropping on-screen chunks. The world-AABB cache is
            // rebuilt (O(voxels)) only when voxel positions actually change
            // (alloc/dealloc, in-place rewrites signalled via
            // `markCullBoundsDirty`), so a static world under a rotating
            // camera pays only O(chunks)/frame here.
            ensureChunkWorldBounds(chunkCount);
            for (int c = 0; c < chunkCount; ++c) {
                const ChunkWorldBounds &wb = m_chunkWorldBounds[c];
                if (wb.empty())
                    continue; // leave the inverted sentinel → chunk never visible
                // Cell-anchor shift (#2545) — matches the compact cull's
                // anchored per-voxel projection, keeping this gate a
                // conservative superset of the per-voxel iso bounds.
                const IsoBounds2D iso = IRMath::isoAABBOfWorldAABBUnderYaw(
                    wb.worldMin_ - vec3(0.5f),
                    wb.worldMax_ - vec3(0.5f),
                    visualYaw
                );
                m_chunkBounds[c].isoMin_ = iso.min_;
                m_chunkBounds[c].isoMax_ = iso.max_;
            }
            // Never cache a per-frame yaw snapshot in the iso bounds; force the
            // next cardinal-path call to rebuild rather than trust stale
            // continuous bounds. (The world-AABB cache is yaw-independent and
            // stays valid across yaw frames — only position changes evict it.)
            m_chunkBoundsDirty = true;
            return;
        }

        // Cardinal path: per-voxel iso expand, cached by cardinal index. The
        // DERIVATION is unchanged from master (same projection, same
        // roundless cardinal rotation, same minDepth_), so the yaw==0 render
        // path is byte-identical; only WHEN a chunk is re-derived changed
        // (#2830). A whole-cache rebuild still runs on the events that
        // invalidate every chunk at once — a cardinal-index change, the
        // continuous-yaw branch's self-invalidation, or a chunk-count change
        // — while an in-place position/alpha rewrite re-derives only the
        // chunks its range touched.
        const bool cardinalChanged = cardinalIndex != m_lastBoundsCardinalIndex;
        const bool fullRebuild = m_chunkBoundsDirty || cardinalChanged ||
                                 static_cast<int>(m_chunkBounds.size()) != chunkCount;
        if (!fullRebuild && m_pendingCardinalChunks.empty()) {
            return;
        }
        m_chunkBounds.resize(static_cast<std::size_t>(chunkCount));
        if (fullRebuild) {
            for (int c = 0; c < chunkCount; ++c) {
                rebuildCardinalChunkBounds(c, cardinalIndex);
            }
            dropPendingChunks(m_pendingCardinalChunks, kCullPendingCardinal);
        } else {
            // Consume ONLY the cardinal bit: the world-AABB consumer runs on
            // different frames and must still see this chunk as pending.
            for (const std::size_t c : m_pendingCardinalChunks) {
                m_chunkCullPending[c] &= static_cast<std::uint8_t>(~kCullPendingCardinal);
                if (static_cast<int>(c) < chunkCount) {
                    rebuildCardinalChunkBounds(static_cast<int>(c), cardinalIndex);
                }
            }
            m_pendingCardinalChunks.clear();
        }
        m_lastBoundsCardinalIndex = cardinalIndex;
        m_chunkBoundsDirty = false;
    }

    // Notification that pool slots [start, start + count) had a value an
    // on-demand cull cache derives from rewritten IN PLACE (no realloc) — a
    // world position (a parent move in `UPDATE_VOXEL_SET_CHILDREN`, a GRID
    // re-voxelize in `REBUILD_GRID_VOXELS`) or a voxel's alpha (any
    // active-mask mutation, hence every set-level edit). Both derived caches
    // read both inputs, so ONE evictor covers both: the split pair this
    // replaced let the cardinal cache freeze while its world-AABB sibling was
    // evicted on the same event (#2830, and see `.claude/rules/cpp-ecs.md`
    // §"A change-gated recompute must trigger on every input its function
    // reads").
    //
    // The range is a notification of COMPLETED writes, not a comparison
    // against a previous pose — it carries no snapshot and is not a dirty
    // flag in the sense the no-dirty-flags rule forbids. Invalidation is per
    // 256-slot chunk (`IRRender::kVoxelChunkSize`), the same granularity the
    // caches are keyed at, so a moving set re-derives its own chunks instead
    // of the whole pool (the +1.331 ms/frame whole-pool rebuild that refuted
    // the first approach). Zero @p count is a no-op; overlapping and
    // duplicate notifications coalesce.
    void markCullBoundsDirty(std::size_t start, std::size_t count) {
        if (count == 0) {
            return;
        }
        const std::size_t poolSize = static_cast<std::size_t>(m_voxelPoolSize);
        // Overflow-safe bounds check: `start + count` can wrap on a bad call.
        IR_ASSERT(
            start <= poolSize && count <= poolSize - start,
            "markCullBoundsDirty out of bounds: start={}, count={}, poolSize={}",
            start,
            count,
            m_voxelPoolSize
        );
        const std::size_t firstChunk = start / IRRender::kVoxelChunkSize;
        const std::size_t lastChunk = (start + count - 1) / IRRender::kVoxelChunkSize;
        if (m_chunkCullPending.size() <= lastChunk) {
            m_chunkCullPending.resize(lastChunk + 1, 0u);
        }
        for (std::size_t c = firstChunk; c <= lastChunk; ++c) {
            markCullChunkPending(c);
        }
    }

    // DEPRECATED — use markCullBoundsDirty(start, count) instead.
    // Kept as a whole-allocated-prefix forwarder so an out-of-tree caller
    // gets the correct (stronger) eviction rather than the half-eviction the
    // name promised. Every in-tree caller migrated in #2830.
    [[deprecated("use markCullBoundsDirty(start, count) instead")]]
    void markChunkWorldBoundsDirty() {
        markAllocatedPrefixCullBoundsDirty();
    }

    // DEPRECATED — use markCullBoundsDirty(start, count) instead.
    // Had zero callers tree-wide before #2830; it is no longer an independent
    // evictor, so the two-cache split it created cannot be got wrong again.
    [[deprecated("use markCullBoundsDirty(start, count) instead")]]
    void markChunkBoundsDirty() {
        markAllocatedPrefixCullBoundsDirty();
    }

    // Per-chunk cull-rebuild work counters (#2830 AC3). A permanent seam
    // rather than a temporary probe, so the cache-locality property is
    // regress-guarded: `test/ecs/chunk_bounds_eviction_test.cpp` asserts that
    // notifying one chunk visits one chunk. O(1) state, incremented once per
    // chunk rebuilt (never per voxel), so there is no per-voxel telemetry on
    // the hot path.
    struct CullRebuildCounters {
        std::size_t cardinalChunks_ = 0;
        std::size_t cardinalSlots_ = 0;
        std::size_t worldChunks_ = 0;
        std::size_t worldSlots_ = 0;
    };

    [[nodiscard]] CullRebuildCounters consumeCullRebuildCounters() {
        const CullRebuildCounters counters = m_cullRebuildCounters;
        m_cullRebuildCounters = CullRebuildCounters{};
        return counters;
    }

    // Seed the conservative origin-centered world-AABB used by the detached
    // re-voxelize cull path (#1556). @p halfExtents are the authored solid's
    // per-axis half-extents about the pool origin; the bound is the cube
    // [-r, r]^3 with r = |halfExtents| (the farthest corner's distance from the
    // origin), which contains the solid under any rotation. Seeded ONCE at pool
    // allocation by SYSTEM_REBUILD_DETACHED_VOXELS — it never changes per frame,
    // so the GPU compute that rewrites binding 5 doesn't need a CPU mirror for
    // the cull. Setting this switches rebuildChunkBounds onto the static-bound
    // branch above; leaving it unset (every non-re-voxelize pool) is
    // byte-identical.
    void setStaticReVoxelizeBound(vec3 halfExtents) {
        const float r = IRMath::length(halfExtents);
        m_staticReVoxelizeBound = ChunkWorldBounds{vec3(-r), vec3(r)};
    }

    bool hasStaticReVoxelizeBound() const {
        return m_staticReVoxelizeBound.has_value();
    }

    // Cull query: true if any chunk overlapping the pool slot range
    // [startIdx, startIdx + count) has an iso-space AABB that intersects
    // `viewport`. Reads the chunk bounds as last computed by
    // `rebuildChunkBounds` (driven by the render pipeline) — a caller in
    // the UPDATE pipeline gets a one-frame-lagged but self-consistent
    // answer (both the bounds and the cull viewport are last-render state).
    // Chunk granularity is conservative: a chunk shared by several voxel
    // sets reports visible if ANY of its voxels are in view, so the gate
    // over-rebuilds rather than dropping geometry. Fail-safe: returns true
    // when the bounds aren't available yet (pre-first-render) or the range
    // runs past the computed chunk set, so geometry is never silently
    // culled before bounds exist. The intersection test mirrors
    // `buildChunkVisibilityMask` in `system_voxel_to_trixel.hpp`.
    bool
    isRangeVisible(std::size_t startIdx, std::size_t count, const IsoBounds2D &viewport) const {
        if (count == 0) {
            return false;
        }
        if (m_chunkBounds.empty()) {
            return true;
        }
        const std::size_t firstChunk = startIdx / IRRender::kVoxelChunkSize;
        const std::size_t lastChunk = (startIdx + count - 1) / IRRender::kVoxelChunkSize;
        for (std::size_t c = firstChunk; c <= lastChunk; ++c) {
            if (c >= m_chunkBounds.size()) {
                return true;
            }
            // Pending invalidation ⇒ admit conservatively (#2830). This gate
            // feeds the UPDATE movers, which run BEFORE the render pipeline
            // re-derives the bounds; answering from a chunk that already owes
            // a recompute is what latched an edited set off-screen (its mover
            // was skipped, so nothing ever moved it back into view). A frozen
            // yes costs one set's re-upload; a frozen no drops its geometry.
            if (c < m_chunkCullPending.size() &&
                (m_chunkCullPending[c] & kCullPendingCardinal) != 0) {
                return true;
            }
            const ChunkBounds &cb = m_chunkBounds[c];
            if (viewport.overlapsAABB(cb.isoMin_, cb.isoMax_)) {
                return true;
            }
        }
        return false;
    }

    // Active-slot mask: 1 bit per pool slot, mirroring `m_voxelColors[i].color_.alpha_ != 0`.
    // The GPU compact shader at `c_voxel_visibility_compact.{glsl,metal}` reads this in place
    // of the per-voxel alpha test (T-287 / #950). CPU storage is `std::vector<uint32_t>`; the
    // shader binds it as `uint activeMask[]` at `kBufferIndex_VoxelActiveMask`.
    const std::vector<std::uint32_t> &getActiveMask() const {
        return m_activeMask;
    }
    std::size_t getActiveMaskSizeBytes() const {
        return m_activeMask.size() * sizeof(std::uint32_t);
    }

    // #2346 — read-and-clear the per-frame active-mask-mutation signal. Consumed
    // once per tick by VOXEL_TO_TRIXEL_STAGE_1 to fold an activation-only edit
    // (activate/deactivate/carve/fillPlane/reshape — none of which queue a
    // position range) into the `storeTiesPossible_` recompute trigger, so a
    // voxel activated onto an already-occupied roundHalfUp cell re-arms the
    // cardinal winner election instead of leaving the last-writer-wins race.
    [[nodiscard]] bool consumeActiveMaskChanged() {
        const bool changed = m_activeMaskChangedThisFrame;
        m_activeMaskChangedThisFrame = false;
        return changed;
    }

    void setActiveBit(std::size_t idx) {
        IR_ASSERT(
            idx < static_cast<std::size_t>(m_voxelPoolSize),
            "setActiveBit out of bounds: idx={}, poolSize={}",
            idx,
            m_voxelPoolSize
        );
        m_activeMask[idx / kVoxelActiveMaskBits] |=
            (std::uint32_t{1} << (idx % kVoxelActiveMaskBits));
        m_activeMaskChangedThisFrame = true; // #2346 — see the member decl.
        markCullBoundsDirty(idx, 1);         // #2830 — alpha is a bounds input.
    }

    void clearActiveBit(std::size_t idx) {
        IR_ASSERT(
            idx < static_cast<std::size_t>(m_voxelPoolSize),
            "clearActiveBit out of bounds: idx={}, poolSize={}",
            idx,
            m_voxelPoolSize
        );
        m_activeMask[idx / kVoxelActiveMaskBits] &=
            ~(std::uint32_t{1} << (idx % kVoxelActiveMaskBits));
        m_activeMaskChangedThisFrame = true; // #2346 — see the member decl.
        markCullBoundsDirty(idx, 1);         // #2830 — alpha is a bounds input.
    }

    // Bulk variants for span-shaped mutations on `C_VoxelSetNew`. The single-bit
    // setters above use one OR/AND per word touched; the range variants below
    // handle the partial-word prefix and suffix once and mass-write the middle
    // words to all-ones / zeros.
    void setActiveMaskRange(std::size_t start, std::size_t count) {
        setMaskRange(start, count, true);
    }

    void clearActiveMaskRange(std::size_t start, std::size_t count) {
        setMaskRange(start, count, false);
    }

    // Recompute the mask bits for `[start, start + count)` from the live
    // `m_voxelColors[i].color_.alpha_` values. Use after a span-shaped write
    // that mixes active and inactive slots (e.g. `C_VoxelSetNew`'s ctor fill
    // or `reshape`/`fillPlane`) — saves the caller from a per-voxel
    // `setActiveBit` / `clearActiveBit` choice.
    void resyncActiveMaskFromColors(std::size_t start, std::size_t count) {
        IR_ASSERT(
            start + count <= static_cast<std::size_t>(m_voxelPoolSize),
            "resyncActiveMaskFromColors out of bounds: start={}, count={}, poolSize={}",
            start,
            count,
            m_voxelPoolSize
        );
        // Notify the whole span once up front rather than per voxel: the
        // per-bit setters below then hit markCullChunkPending's already-pending
        // early-out, so this loop keeps its current per-voxel cost (#2830).
        markCullBoundsDirty(start, count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t idx = start + i;
            if (m_voxelColors[idx].color_.alpha_ != 0) {
                setActiveBit(idx);
            } else {
                clearActiveBit(idx);
            }
        }
    }

    // Cap on queued position ranges. The fixed-timestep loop runs
    // UPDATE_VOXEL_SET_CHILDREN once per update tick, and a slow render
    // frame accumulates several update ticks before VOXEL_TO_TRIXEL_STAGE_1
    // drains the queue. Every moving voxel set re-queues its range each
    // tick, so the list can reach (update ticks) × (moving sets) entries —
    // millions in a stress scene. Past this cap the sort + coalesce in
    // `flushPendingPositionRanges` costs far more than one whole-live-range
    // upload, so the flusher treats a saturated queue as "re-upload
    // everything" and further queue calls become no-ops (the full upload
    // covers the dropped ranges anyway).
    static constexpr std::size_t kMaxPendingPositionRanges = 8192;

    // Queue a slice of position-globals to upload to the GPU position
    // SSBO on the next `flushPendingPositionRanges` call. Mirrors the
    // pending-list-flush pattern documented in `cpp-ecs.md`: the
    // mutating system (UPDATE_VOXEL_SET_CHILDREN) queues the affected
    // slice; the GPU-buffer-owning system (VOXEL_TO_TRIXEL_STAGE_1)
    // coalesces contiguous queued ranges into one `subData` per run.
    // Saturating `kMaxPendingPositionRanges` switches the flusher to a
    // single whole-buffer upload — see `flushPendingPositionRanges`.
    void queuePositionRange(size_t startIdx, size_t count) {
        if (count == 0) {
            return;
        }
        // #2830 — cull invalidation is independent of the GPU upload queue and
        // must happen BEFORE the saturation early-return below. The flusher
        // drops/coalesces queued ranges and runs AFTER the chunk-mask rebuild,
        // so deferring invalidation to the flush would lose it exactly in the
        // busy scenes that saturate.
        markCullBoundsDirty(startIdx, count);
        if (m_pendingPositionRanges.size() >= kMaxPendingPositionRanges) {
            return;
        }
        m_pendingPositionRanges.emplace_back(startIdx, count);
    }

    const std::vector<std::pair<size_t, size_t>> &getPendingPositionRanges() const {
        return m_pendingPositionRanges;
    }

    std::vector<std::pair<size_t, size_t>> &getPendingPositionRanges() {
        return m_pendingPositionRanges;
    }

    void clearPendingPositionRanges() {
        m_pendingPositionRanges.clear();
    }

    // Per-voxel GPU transform-slot index, consumed by UPDATE_VOXEL_POSITIONS_GPU
    // (packed into the `.w` lane of the local-position buffer,
    // kBufferIndex_LocalVoxelPositions = 17). Defaults to `kVoxelTransformStatic`
    // for every voxel, so an untouched pool leaves the GPU prepass a no-op and
    // binding 5 fully CPU-owned (byte-identical to the pre-prepass path). A voxel
    // set marks its range GPU-dynamic by pointing it at an EntityTransformBuffer slot.
    const std::vector<std::uint32_t> &getTransformIndices() const {
        return m_voxelTransformIndices;
    }

    // Point a voxel range at a GPU transform slot (or back at
    // `kVoxelTransformStatic`). Queues the slice for upload via the same
    // pending-range flush the positions use — set once at voxel-set creation,
    // so a static scene queues nothing and pays zero bytes/frame.
    void setTransformIndexForRange(size_t startIdx, size_t count, std::uint32_t transformIndex) {
        if (count == 0) {
            return;
        }
        IR_ASSERT(
            startIdx + count <= m_voxelTransformIndices.size(),
            "setTransformIndexForRange out of bounds: startIdx={}, count={}, poolSize={}",
            startIdx,
            count,
            m_voxelTransformIndices.size()
        );
        IR_ASSERT(
            transformIndex == IRRender::kVoxelTransformStatic ||
                transformIndex < static_cast<std::uint32_t>(IRRender::kMaxGpuVoxelTransforms),
            "setTransformIndexForRange: transformIndex={} out of range (kMaxGpuVoxelTransforms={})",
            transformIndex,
            IRRender::kMaxGpuVoxelTransforms
        );
        std::fill(
            m_voxelTransformIndices.begin() + startIdx,
            m_voxelTransformIndices.begin() + startIdx + count,
            transformIndex
        );
        if (m_pendingTransformIndexRanges.size() < kMaxPendingPositionRanges) {
            m_pendingTransformIndexRanges.emplace_back(startIdx, count);
        }
    }

    // Per-voxel variant of `setTransformIndexForRange` for skeletal bone→slot
    // seeding (#605 Phase 2.3 / #1605): voxel `startIdx + i` points at
    // `indices[i]`, so each voxel of one set can follow a different joint slot.
    // Queues the same pending-range flush.
    void setTransformIndicesForRange(size_t startIdx, std::span<const std::uint32_t> indices) {
        if (indices.empty()) {
            return;
        }
        IR_ASSERT(
            startIdx + indices.size() <= m_voxelTransformIndices.size(),
            "setTransformIndicesForRange out of bounds: startIdx={}, count={}, poolSize={}",
            startIdx,
            indices.size(),
            m_voxelTransformIndices.size()
        );
        // Caller guarantees every index satisfies kVoxelTransformStatic || <
        // kMaxGpuVoxelTransforms. The loop assert below enforces the same invariant as
        // setTransformIndexForRange in debug builds.
        for (std::size_t i = 0; i < indices.size(); ++i) {
            IR_ASSERT(
                indices[i] == IRRender::kVoxelTransformStatic ||
                    indices[i] < static_cast<std::uint32_t>(IRRender::kMaxGpuVoxelTransforms),
                "setTransformIndicesForRange: indices[{}]={} out of range "
                "(kMaxGpuVoxelTransforms={})",
                i,
                indices[i],
                IRRender::kMaxGpuVoxelTransforms
            );
        }
        std::copy(indices.begin(), indices.end(), m_voxelTransformIndices.begin() + startIdx);
        if (m_pendingTransformIndexRanges.size() < kMaxPendingPositionRanges) {
            m_pendingTransformIndexRanges.emplace_back(startIdx, indices.size());
        }
    }

    const std::vector<std::pair<size_t, size_t>> &getPendingTransformIndexRanges() const {
        return m_pendingTransformIndexRanges;
    }

    std::vector<std::pair<size_t, size_t>> &getPendingTransformIndexRanges() {
        return m_pendingTransformIndexRanges;
    }

    void clearPendingTransformIndexRanges() {
        m_pendingTransformIndexRanges.clear();
    }

  private:
    // Zero-initialized so the default ctor leaves a consistently-EMPTY pool.
    // Left uninitialized it fed garbage to every bounds assert that reads it
    // (`markCullBoundsDirty`), the same class of defect as m_voxelPoolSize3D
    // below (#2043). The ivec3 ctor's init list overrides this.
    int m_voxelPoolSize = 0;
    // 3D pool dimensions, kept alongside the scalar count. Read by the detached
    // re-voxelize footprint cap (subdivisionCap, #1570 D2). Must be initialized —
    // either via the ivec3 numVoxels ctor or the {0,0,0} default initializer.
    // Left uninitialized it fed garbage to subdivisionCap, non-deterministically
    // pinning the cap (the #2043 root cause).
    ivec3 m_voxelPoolSize3D{0, 0, 0};
    bool m_entityIdsDirty = true;
    bool m_chunkBoundsDirty = true;
    CardinalIndex m_lastBoundsCardinalIndex = CardinalIndex::k0;

    std::vector<EntityId> m_voxelEntities;
    std::vector<IRRender::VoxelGpuPosition> m_voxelPositions;
    std::vector<vec3> m_voxelPositionsOffset;
    std::vector<IRRender::VoxelGpuPosition> m_voxelPositionsGlobal;
    std::vector<C_Voxel> m_voxelColors;
    std::vector<std::uint32_t> m_activeMask;
    // #2346 — set by any active-mask mutation (setActiveBit / clearActiveBit /
    // setActiveMaskRange / clearActiveMaskRange, hence resyncActiveMaskFromColors),
    // consumed+cleared once per frame by VOXEL_TO_TRIXEL_STAGE_1 via
    // consumeActiveMaskChanged(). This is NOT a GPU-sync dirty flag (the active
    // mask uploads unconditionally every frame regardless): it gates only the
    // CPU-side tie-possibility recompute — the "did an activation change" analog
    // of `positionsChanged = !m_pendingPositionRanges.empty()`, which likewise
    // re-triggers the scan without owning any GPU upload. Push-at-mutation,
    // consume-once — the sanctioned pattern per .claude/rules/cpp-ecs.md, not a
    // per-frame CPU→GPU sync gate.
    bool m_activeMaskChangedThisFrame = false;
    std::vector<std::pair<size_t, size_t>> m_freeVoxelSpans;
    std::map<size_t, std::set<std::pair<size_t, size_t>>> m_freeSpanLookup;
    std::vector<ChunkBounds> m_chunkBounds;
    // Cached static (camera-independent) world-AABB per chunk, projected under
    // the live yaw by the continuous-yaw cull (#1439). Rebuilt only when voxel
    // positions change, so a static world under a rotating camera skips the
    // per-voxel re-projection entirely.
    std::vector<ChunkWorldBounds> m_chunkWorldBounds;
    bool m_chunkWorldBoundsDirty = true;
    // Per-chunk pending-invalidation bits for the two derived cull caches
    // (#2830). One byte per 256-slot chunk, sized on demand and reused; bit 0
    // says the cardinal iso bounds owe this chunk a recompute, bit 1 says the
    // world-AABB cache does. The bits are SEPARATE because the two consumers
    // run on different frames — the cardinal branch of `rebuildChunkBounds`
    // against `ensureChunkWorldBounds` under continuous yaw — and each clears
    // only its own, so consuming one can never discard the other's work.
    static constexpr std::uint8_t kCullPendingCardinal = 1u;
    static constexpr std::uint8_t kCullPendingWorld = 2u;
    static constexpr std::uint8_t kCullPendingBoth = kCullPendingCardinal | kCullPendingWorld;
    std::vector<std::uint8_t> m_chunkCullPending;
    // Chunk indices whose corresponding bit is set, so a consumer walks its
    // pending set instead of scanning the flag array. A chunk is appended only
    // on the 0 -> 1 transition of its bit, so the lists carry no duplicates
    // and overlapping notifications coalesce. Capacity is retained across
    // frames (cleared, never shrunk) — the steady state allocates nothing.
    std::vector<std::size_t> m_pendingCardinalChunks;
    std::vector<std::size_t> m_pendingWorldChunks;
    CullRebuildCounters m_cullRebuildCounters{};
    // Conservative origin-centered world-AABB for a detached re-voxelize pool
    // (#1556). Set once by setStaticReVoxelizeBound; when present, rebuildChunkBounds
    // ignores the (GPU-owned, CPU-stale) per-voxel globals and projects this
    // instead. nullopt for every other pool → byte-identical cull.
    std::optional<ChunkWorldBounds> m_staticReVoxelizeBound;
    // Per-frame queue of position-global slices whose CPU contents were
    // rewritten since the last GPU flush. Drained + coalesced by
    // VOXEL_TO_TRIXEL_STAGE_1; capacity is preserved across frames so
    // a steady-state moving scene avoids per-frame allocation.
    std::vector<std::pair<size_t, size_t>> m_pendingPositionRanges;
    // Per-voxel GPU transform-slot index (see `setTransformIndexForRange`).
    std::vector<std::uint32_t> m_voxelTransformIndices;
    // Per-frame queue of transform-index slices that pack transform-slot indices
    // into the .w lane of binding 17 (kBufferIndex_LocalVoxelPositions);
    // mirrors `m_pendingPositionRanges`. Empty for static scenes.
    std::vector<std::pair<size_t, size_t>> m_pendingTransformIndexRanges;

    int m_voxelPoolIndex = 0;

    // Count of voxels in this pool carrying a non-zero per-trixel priority (#2155).
    // Maintained push-at-mutation via adjustPerTrixelPriorityVoxelCount (called by
    // the C_VoxelSetNew priority mutators + set teardown). hasPerTrixelPriority()
    // reads it once per frame in VOXEL_TO_TRIXEL_STAGE_1 to stamp the canvas.
    int m_perTrixelPriorityVoxelCount = 0;

    void updateFreeSpanLookup(size_t startIndex, size_t size) {
        m_freeSpanLookup[size].insert({startIndex, size});
    }

    // Mark one chunk pending for BOTH caches. Both read the same two inputs
    // (position, alpha), so every notification arms both; the early-out makes
    // a repeat notification for an already-pending chunk ~free, which is what
    // keeps the per-voxel `setActiveBit` path's added cost negligible.
    void markCullChunkPending(std::size_t chunk) {
        std::uint8_t &flags = m_chunkCullPending[chunk];
        if (flags == kCullPendingBoth) {
            return;
        }
        if ((flags & kCullPendingCardinal) == 0) {
            m_pendingCardinalChunks.push_back(chunk);
            flags |= kCullPendingCardinal;
        }
        if ((flags & kCullPendingWorld) == 0) {
            m_pendingWorldChunks.push_back(chunk);
            flags |= kCullPendingWorld;
        }
    }

    // Backing for the two deprecated no-argument evictors: notify the whole
    // allocated prefix, which is the strongest eviction the old signature can
    // express.
    void markAllocatedPrefixCullBoundsDirty() {
        markCullBoundsDirty(0, static_cast<std::size_t>(m_voxelPoolIndex));
    }

    // Clear one consumer's bit across its whole pending list, then empty it.
    // Used after a whole-cache rebuild has covered every chunk.
    void dropPendingChunks(std::vector<std::size_t> &pending, std::uint8_t bit) {
        for (const std::size_t c : pending) {
            m_chunkCullPending[c] &= static_cast<std::uint8_t>(~bit);
        }
        pending.clear();
    }

    // Half-open slot range backing @p chunk, clamped to the allocated prefix.
    // A chunk past the prefix yields an empty range, so it re-derives to the
    // inverted sentinel and reads as never-visible.
    std::pair<int, int> chunkSlotRange(int chunk) const {
        const int begin = chunk * IRRender::kVoxelChunkSize;
        const int end = IRMath::min(begin + IRRender::kVoxelChunkSize, m_voxelPoolIndex);
        return {begin, IRMath::max(begin, end)};
    }

    // Re-derive ONE chunk's cardinal iso bounds. Reduces into a stack-local
    // `ChunkBounds` and stores once, rather than read-modify-writing the cache
    // entry per voxel — measured at roughly half the whole-pool loop's cost,
    // which is what makes a correct trigger set affordable (#2830). The
    // projection, the cardinal rotation and the minDepth_ fold are verbatim
    // from the pre-#2830 whole-pool loop, so the values are identical.
    void rebuildCardinalChunkBounds(int chunk, CardinalIndex cardinalIndex) {
        const auto [begin, end] = chunkSlotRange(chunk);
        ++m_cullRebuildCounters.cardinalChunks_;
        m_cullRebuildCounters.cardinalSlots_ += static_cast<std::size_t>(end - begin);
        ChunkBounds bounds;
        for (int i = begin; i < end; ++i) {
            if (m_voxelColors[i].color_.alpha_ == 0)
                continue;
            vec3 pos = m_voxelPositionsGlobal[i].pos_;
            if (cardinalIndex != CardinalIndex::k0) {
                // Plain cardinal rotation — no lower-corner shift (#2545);
                // mirrors the stage-1/2 store cell and the compact cull.
                pos = IRMath::rotateCardinalZ(pos, cardinalIndex);
            }
            bounds.expand(IRMath::pos3DtoPos2DIso(pos));
            // Track the chunk's front-most raw iso depth in the same projection,
            // for the occlusion pre-pass's Hi-Z compare (#1294 child 2/3).
            bounds.minDepth_ =
                IRMath::min(bounds.minDepth_, static_cast<float>(IRMath::pos3DtoDistance(pos)));
        }
        m_chunkBounds[static_cast<std::size_t>(chunk)] = bounds;
    }

    // Re-derive ONE chunk's static world-AABB. Same chunk-local reduction as
    // the cardinal twin; the projection is unchanged from the pre-#2830
    // whole-pool sweep.
    void rebuildChunkWorldBounds(int chunk) {
        const auto [begin, end] = chunkSlotRange(chunk);
        ++m_cullRebuildCounters.worldChunks_;
        m_cullRebuildCounters.worldSlots_ += static_cast<std::size_t>(end - begin);
        ChunkWorldBounds bounds;
        for (int i = begin; i < end; ++i) {
            if (m_voxelColors[i].color_.alpha_ == 0)
                continue;
            bounds.expand(m_voxelPositionsGlobal[i].pos_);
        }
        m_chunkWorldBounds[static_cast<std::size_t>(chunk)] = bounds;
    }

    // Rebuilds the per-chunk static world-AABB cache from the live voxels'
    // world positions. The result is camera-independent, so the continuous-yaw
    // cull reuses it across yaw frames (#1439). Since #2830 the steady state is
    // O(pending chunks), not O(pool): a whole-cache sweep runs only when the
    // cache was never built or its chunk count changed, and an in-place
    // position/alpha rewrite re-derives only the chunks its range touched.
    void ensureChunkWorldBounds(int chunkCount) {
        const bool fullRebuild =
            m_chunkWorldBoundsDirty || static_cast<int>(m_chunkWorldBounds.size()) != chunkCount;
        if (!fullRebuild && m_pendingWorldChunks.empty()) {
            return;
        }
        m_chunkWorldBounds.resize(static_cast<std::size_t>(chunkCount));
        if (fullRebuild) {
            for (int c = 0; c < chunkCount; ++c) {
                rebuildChunkWorldBounds(c);
            }
            dropPendingChunks(m_pendingWorldChunks, kCullPendingWorld);
        } else {
            // Consume ONLY the world bit — the cardinal consumer may not have
            // run yet and must still see these chunks as pending.
            for (const std::size_t c : m_pendingWorldChunks) {
                m_chunkCullPending[c] &= static_cast<std::uint8_t>(~kCullPendingWorld);
                if (static_cast<int>(c) < chunkCount) {
                    rebuildChunkWorldBounds(static_cast<int>(c));
                }
            }
            m_pendingWorldChunks.clear();
        }
        m_chunkWorldBoundsDirty = false;
    }

    void setMaskRange(std::size_t start, std::size_t count, bool value) {
        if (count == 0) {
            return;
        }
        // #2346 — flag here too: the whole-word middle path below writes
        // `m_activeMask` directly, bypassing the per-bit setters that flag.
        m_activeMaskChangedThisFrame = true;
        // #2830 — same reason: the whole-word middle path never reaches the
        // per-bit setters, so the bounds notification has to happen here. One
        // whole-range call also short-circuits the prefix/suffix bits' own.
        markCullBoundsDirty(start, count);
        IR_ASSERT(
            start + count <= static_cast<std::size_t>(m_voxelPoolSize),
            "setMaskRange out of bounds: start={}, count={}, poolSize={}",
            start,
            count,
            m_voxelPoolSize
        );
        const std::size_t end = start + count;
        std::size_t idx = start;
        // Partial-word prefix and a possible all-in-one-word path.
        while (idx < end && (idx % kVoxelActiveMaskBits) != 0) {
            if (value) {
                setActiveBit(idx);
            } else {
                clearActiveBit(idx);
            }
            ++idx;
        }
        // Whole-word middle.
        while (idx + kVoxelActiveMaskBits <= end) {
            m_activeMask[idx / kVoxelActiveMaskBits] =
                value ? std::numeric_limits<std::uint32_t>::max() : 0u;
            idx += kVoxelActiveMaskBits;
        }
        // Partial-word suffix.
        while (idx < end) {
            if (value) {
                setActiveBit(idx);
            } else {
                clearActiveBit(idx);
            }
            ++idx;
        }
    }

    std::optional<std::pair<size_t, size_t>> findFreeSpan(size_t requestedSize) const {
        auto it = m_freeSpanLookup.lower_bound(requestedSize);
        if (it != m_freeSpanLookup.end()) {
            std::pair<size_t, size_t> span = *it->second.begin();
            // Return the span if it's not larger than needed
            if (span.second <= requestedSize) {
                return span;
            }
        }
        return std::nullopt; // No suitable free span found
    }
};
} // namespace IRComponents

#endif /* COMPONENT_VOXEL_POOL_H */

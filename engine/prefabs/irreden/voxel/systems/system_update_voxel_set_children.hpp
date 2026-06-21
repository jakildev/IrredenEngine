#ifndef SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H
#define SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/components/component_player.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/sun_shadow_constants.hpp>

#include <cstddef>
#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {
template <> struct System<UPDATE_VOXEL_SET_CHILDREN> {
    // #1803 — this 262K-entity position loop is one of the two dominant
    // UPDATE costs (#1740), so it runs Concurrency::PARALLEL_FOR. The tick
    // keeps the per-entity-id form for the one-time owner registration
    // (`setEntityIdForRange` → GPU picking buffer), which makes the
    // registration validator demand the `IRSystem::ParallelSafe` opt-in. The
    // body is audited thread-safe; the two architecture blockers the plan
    // missed (worker-side pool lookup, shared `queuePositionRange` vector)
    // are resolved by `beginTick` pre-resolution + a per-worker deferred
    // merge below. See the PR #1891 architect direction (1a + 2a + 3).
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    // One deferred `queuePositionRange` call. The pool's
    // `m_pendingPositionRanges.emplace_back` is the lone cross-set shared-
    // vector write in the tick, so workers stage into their own slot and the
    // main thread merges in `endTick` (the pending set is order-independent
    // downstream). Keyed by pool because the canvas→pool map spans every
    // canvas's pool, and different sets in one tick may target different pools.
    struct PendingRange {
        C_VoxelPool *pool_ = nullptr;
        std::size_t startIdx_ = 0;
        std::size_t count_ = 0;
    };

    // Resolved on the main thread in beginTick, read-only in the worker tick.
    // The workers never touch EntityManager's hash map (getComponent is not
    // worker-safe under PARALLEL_FOR — engine/system/CLAUDE.md).
    std::unordered_map<IREntity::EntityId, C_VoxelPool *> canvasToPool_;
    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    // Fast path for the dominant case (almost every set targets the active
    // canvas): skip the per-entity map lookup and read this cached pointer.
    // The map is only consulted for sets with an explicit canvasEntity_ override.
    C_VoxelPool *activePool_ = nullptr;

    // Per-worker pending-range accumulators, indexed by IRJob::workerId()
    // (slot 0 = main thread, 1..N = IRJob workers). Sized in beginTick;
    // capacity is retained across frames (cleared, never shrunk) so the
    // steady state is allocation-free.
    std::vector<std::vector<PendingRange>> pendingByWorker_;

    // Cull viewport captured from the previous render frame (one-frame lag,
    // consistent with the GPU cull and invisible for camera pans). cullValid_
    // is false until the render pipeline produces a non-zero canvas size.
    bool cullValid_ = false;
    IsoBounds2D viewport_ = {};

    void beginTick() {
        // Pre-resolve every canvas → pool mapping on the main thread. One
        // pool per canvas entity (voxel/CLAUDE.md), so iterating C_VoxelPool
        // yields the complete map the workers read by canvas id. Cleared each
        // frame; the map capacity is reused.
        canvasToPool_.clear();
        IREntity::forEachComponent<C_VoxelPool>(
            [this](IREntity::EntityId &canvas, C_VoxelPool &pool) { canvasToPool_[canvas] = &pool; }
        );
        // Headless-safe snapshot (kNullEntity pre-canvas); a set with no
        // explicit canvasEntity_ falls back to this in the tick.
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        auto activeIt = canvasToPool_.find(activeCanvas_);
        activePool_ = activeIt != canvasToPool_.end() ? activeIt->second : nullptr;

        // Size + clear the per-worker accumulators (workerCount() is 0 with no
        // job pool → one main-thread slot, matching workerId() == 0).
        const std::size_t slots = static_cast<std::size_t>(IRJob::workerCount()) + 1u;
        if (pendingByWorker_.size() < slots) {
            pendingByWorker_.resize(slots);
        }
        for (std::vector<PendingRange> &worker : pendingByWorker_) {
            worker.clear();
        }

        // Mirrors the cull gate in system_rebuild_grid_voxels.hpp — same
        // shadow-feeder-expanded viewport so off-screen casters whose position
        // update is required for shadow projection still upload. getCullViewport()
        // holds last render frame's snapshot when called from the UPDATE pipeline.
        const IRRender::CullViewportState &cull = IRRender::getCullViewport();
        cullValid_ = cull.canvasSize_.x > 0 && cull.canvasSize_.y > 0;
        if (cullValid_) {
            viewport_ = IRPrefab::SunShadow::shadowFeederCullViewport(
                IRRender::kCullChunkMargin,
                IRPrefab::SunShadow::frameShadowFeederParams(),
                cull
            );
        }
    }

    void tick(
        IREntity::EntityId &entityId,
        C_VoxelSetNew &voxelSet,
        const C_WorldTransform &worldTransform
    ) {
        if (voxelSet.numVoxels_ <= 0) {
            return;
        }

        // Resolve the pool from the read-only map built on the main thread in
        // beginTick — no EntityManager access from the worker. The active
        // canvas is the dominant case, so take the cached pointer and only hit
        // the map for an explicit canvasEntity_ override.
        IREntity::EntityId canvas = voxelSet.canvasEntity_;
        C_VoxelPool *poolPtr = activePool_;
        if (canvas != IREntity::kNullEntity && canvas != activeCanvas_) {
            auto poolIt = canvasToPool_.find(canvas);
            poolPtr = poolIt != canvasToPool_.end() ? poolIt->second : nullptr;
        }
        if (poolPtr == nullptr) {
            return; // no pool for this canvas (headless / pre-canvas set)
        }
        C_VoxelPool &pool = *poolPtr;

        // Cull gate: skip the CPU→GPU position update when none of this
        // set's pool chunks are in the camera viewport. On-screen sets
        // re-upload every frame (unconditional per ECS no-dirty-flags rule).
        // cullValid_ is false before the first render frame — fail-safe to
        // doing the work rather than silently dropping geometry at startup.
        if (cullValid_ && !pool.isRangeVisible(
                              voxelSet.voxelStartIdx_,
                              static_cast<std::size_t>(voxelSet.numVoxels_),
                              viewport_
                          )) {
            return;
        }

        // updateAsChild writes only this set's disjoint pool span
        // [voxelStartIdx_, voxelStartIdx_ + safeCount) — safe under
        // concurrency. Returns the number of positions actually written
        // (may be < numVoxels_ if the pool-bounds guard fires).
        const int writtenCount = voxelSet.updateAsChild(
            worldTransform.translation_,
            pool.getPositionGlobals(),
            pool.getPositions(),
            pool.getPositionOffsets()
        );
        // A GPU-transform-indirected set (#1396) has binding 5 written by the
        // UPDATE_VOXEL_POSITIONS_GPU prepass each frame. We still recompute its
        // CPU global mirror above (a sane translation-only fallback for the
        // STAGE_1 canvas-switch re-seed, and for cull/picking), but we must NOT
        // queue it for the steady-state binding-5 flush — that flush runs after
        // the prepass in the RENDER pipeline and would clobber the GPU positions.
        // Both cases (static + GPU-slotted) defer to endTick via pendingByWorker_:
        // count_ > 0 means "queue + dirty"; count_ == 0 means "dirty only". This
        // keeps markChunkWorldBoundsDirty() on the main thread and off the hot
        // concurrent path (eliminates the same-value concurrent store TSan would flag).
        if (writtenCount > 0) {
            const bool isStatic = voxelSet.gpuTransformSlot_ == IRRender::kVoxelTransformStatic;
            pendingByWorker_[static_cast<std::size_t>(IRJob::workerId())].push_back(PendingRange{
                &pool,
                isStatic ? voxelSet.voxelStartIdx_ : std::size_t{0},
                isStatic ? static_cast<std::size_t>(writtenCount) : std::size_t{0}
            });
        }
        if (voxelSet.ownerEntityId_ == IREntity::kNullEntity && entityId != IREntity::kNullEntity &&
            voxelSet.numVoxels_ > 0) {
            // Disjoint per-set span fill + idempotent dirty bool — benign
            // under concurrency (each set owns [voxelStartIdx_, +numVoxels_)).
            voxelSet.ownerEntityId_ = entityId;
            pool.setEntityIdForRange(
                voxelSet.voxelStartIdx_,
                static_cast<std::size_t>(voxelSet.numVoxels_),
                entityId
            );
        }
    }

    void endTick() {
        // Main-thread merge. For each staged entry: queue the position range when
        // count_ > 0 (static voxel set), and always evict the chunk world-AABB
        // cache (markChunkWorldBoundsDirty). Dirty marking runs here, not in tick,
        // to keep the bool write off the concurrent path (#1803 TSan note).
        for (std::vector<PendingRange> &worker : pendingByWorker_) {
            for (const PendingRange &range : worker) {
                if (range.count_ > 0) {
                    range.pool_->queuePositionRange(range.startIdx_, range.count_);
                }
                range.pool_->markChunkWorldBoundsDirty();
            }
        }
    }

    static SystemId create() {
        // ParallelSafe: the body is audited thread-safe (disjoint span writes +
        // a per-worker deferred merge for the one shared-vector hazard). The
        // tag opts past the PARALLEL_FOR + usesEntityId_ validator and is
        // stripped from the archetype/tick signature by detail::FilterTags_t
        // (ir_system.hpp).
        return registerSystem<
            UPDATE_VOXEL_SET_CHILDREN,
            C_VoxelSetNew,
            C_WorldTransform,
            ParallelSafe>("UpdateVoxelSetChildren");
    }
};
} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */

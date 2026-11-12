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
#include <irreden/system/components/component_static.hpp>
#include <irreden/voxel/voxel_owner.hpp>

#include <cstddef>
#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {
template <> struct System<UPDATE_VOXEL_SET_CHILDREN> {
    // A deriving system for static residents (docs/design/static-residents.md):
    // it excludes C_Settled, so a settled entity's positions are written
    // and queued once, when it settles. The tick is whole-node so the
    // dynamic / settling decision is one branch per archetype node, never a
    // per-row test; it fans its rows out across the worker pool itself, in
    // `kGrainSize` chunks. The row bodies are thread-safe: `beginTick`
    // pre-resolves the pool lookup on the main thread and a per-worker
    // deferred merge covers the shared `queuePositionRange` vector.
    static constexpr int kGrainSize = kDefaultGrainSize;

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
        // GPU-transform-indirected sets update the CPU mirror without uploading
        // binding 5. Retain the real span for cull invalidation independently of
        // the upload decision.
        bool upload_ = false;
    };

    static void appendPendingRange(std::vector<PendingRange> &pending, PendingRange range) {
        if (!pending.empty()) {
            PendingRange &last = pending.back();
            if (last.pool_ == range.pool_ && last.upload_ == range.upload_ &&
                last.startIdx_ + last.count_ == range.startIdx_) {
                last.count_ += range.count_;
                return;
            }
        }
        pending.push_back(range);
    }

    // Resolved on the main thread in beginTick, read-only in the worker tick.
    // The workers never touch EntityManager's hash map (getComponent is not
    // worker-safe under PARALLEL_FOR — engine/system/CLAUDE.md).
    std::unordered_map<IREntity::EntityId, C_VoxelPool *> canvasToPool_;
    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    // Fast path for the dominant case (almost every set targets the active
    // canvas): skip the per-entity map lookup and read this cached pointer.
    // The map is only consulted for sets with an explicit canvasEntity_ override.
    C_VoxelPool *activePool_ = nullptr;
    // Null until a registration names C_Static; no node can hold it before.
    IREntity::ComponentId staticTag_ = IREntity::kNullComponent;

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
        staticTag_ = IREntity::getEntityManager().findComponentType<C_Static>();

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
        const Archetype &type,
        std::vector<IREntity::EntityId> &entities,
        std::vector<C_VoxelSetNew> &voxelSets,
        const std::vector<C_WorldTransform> &worldTransforms
    ) {
        // The archetype excludes C_Settled, so a node carrying C_Static
        // holds settling rows only.
        const bool settling =
            staticTag_ != IREntity::kNullComponent && type.contains(staticTag_);
        const int rowCount = static_cast<int>(entities.size());
        const auto rows = [&](int rowBegin, int rowEnd) {
            if (settling) {
                for (int i = rowBegin; i < rowEnd; ++i) {
                    tickSettlingRow(entities[i], voxelSets[i], worldTransforms[i]);
                }
            } else {
                for (int i = rowBegin; i < rowEnd; ++i) {
                    tickDynamicRow(entities[i], voxelSets[i], worldTransforms[i]);
                }
            }
        };
        if (rowCount > kGrainSize && IRJob::workerCount() > 0 && IRJob::isMainThread()) {
            IRJob::parallelFor(0, rowCount, kGrainSize, rows);
        } else {
            rows(0, rowCount);
        }
    }

    // A dynamic entity's set: frozen while hidden or LOD-culled, and skipped
    // while none of its pool chunks are in the shadow-feeder cull viewport.
    void tickDynamicRow(
        IREntity::EntityId entityId, C_VoxelSetNew &voxelSet, const C_WorldTransform &worldTransform
    ) {
        if (!voxelSet.renders() || voxelSet.numVoxels_ <= 0) {
            return;
        }
        C_VoxelPool *poolPtr = poolFor(voxelSet);
        if (poolPtr == nullptr) {
            return; // no pool for this canvas (headless / pre-canvas set)
        }
        // On-screen sets re-upload every frame (unconditional per ECS
        // no-dirty-flags rule). cullValid_ is false before the first render
        // frame — fail-safe to doing the work rather than silently dropping
        // geometry at startup.
        if (cullValid_ && !poolPtr->isRangeVisible(
                              voxelSet.voxelStartIdx_,
                              static_cast<std::size_t>(voxelSet.allocatedVoxels_),
                              viewport_
                          )) {
            return;
        }
        writeRow(entityId, voxelSet, worldTransform, *poolPtr);
    }

    // A settling static entity's set: no later dispatch makes up for a
    // skipped one, so neither the cull gate nor the `renders()` gate applies
    // and a hidden or culled set is written too. A ghost-held set keeps its
    // ghost positions; the fog evaluator holds the entity while that pose is
    // not its own.
    void tickSettlingRow(
        IREntity::EntityId entityId, C_VoxelSetNew &voxelSet, const C_WorldTransform &worldTransform
    ) {
        if (voxelSet.ghostHeld_ || voxelSet.numVoxels_ <= 0) {
            return;
        }
        C_VoxelPool *poolPtr = poolFor(voxelSet);
        if (poolPtr == nullptr) {
            return;
        }
        writeRow(entityId, voxelSet, worldTransform, *poolPtr);
    }

    // Resolved from the read-only map built on the main thread in beginTick —
    // no EntityManager access from a worker. The active canvas is the
    // dominant case, so take the cached pointer and only hit the map for an
    // explicit canvasEntity_ override.
    C_VoxelPool *poolFor(const C_VoxelSetNew &voxelSet) const {
        const IREntity::EntityId canvas = voxelSet.canvasEntity_;
        if (canvas == IREntity::kNullEntity || canvas == activeCanvas_) {
            return activePool_;
        }
        const auto poolIt = canvasToPool_.find(canvas);
        return poolIt != canvasToPool_.end() ? poolIt->second : nullptr;
    }

    void writeRow(
        IREntity::EntityId entityId,
        C_VoxelSetNew &voxelSet,
        const C_WorldTransform &worldTransform,
        C_VoxelPool &pool
    ) {
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
        // A GPU-transform-indirected set has binding 5 written by the
        // UPDATE_VOXEL_POSITIONS_GPU prepass each frame. Its CPU global mirror
        // is still recomputed (a sane translation-only fallback for the
        // STAGE_1 canvas-switch re-seed, and for cull/picking), but it must NOT
        // be queued for the steady-state binding-5 flush — that flush runs after
        // the prepass in the RENDER pipeline and would clobber the GPU positions.
        // Both cases (static + GPU-slotted) defer to endTick via pendingByWorker_,
        // carrying the real written range plus an upload_ flag: the cull caches
        // must follow the rewritten positions in BOTH cases, only the binding-5
        // upload is static-only. This keeps markCullBoundsDirty() on the main
        // thread and off the hot concurrent path (eliminates the same-value
        // concurrent store TSan would flag).
        if (writtenCount > 0) {
            const bool isStatic = voxelSet.gpuTransformSlot_ == IRRender::kVoxelTransformStatic;
            appendPendingRange(
                pendingByWorker_[static_cast<std::size_t>(IRJob::workerId())],
                PendingRange{
                    &pool,
                    voxelSet.voxelStartIdx_,
                    static_cast<std::size_t>(writtenCount),
                    isStatic
                }
            );
        }
        if (voxelSet.ownerEntityId_ == IREntity::kNullEntity && entityId != IREntity::kNullEntity &&
            voxelSet.numVoxels_ > 0) {
            // Disjoint per-set span fill + idempotent dirty bool — benign
            // under concurrency (each set owns [voxelStartIdx_, +allocatedVoxels_)).
            IRPrefab::Voxel::adoptOwner(voxelSet, entityId, pool);
        }
    }

    void endTick() {
        // Main-thread merge. Every staged entry invalidates the cull caches over
        // the range it actually rewrote; only a static set additionally queues
        // that range for the binding-5 upload. Invalidation runs here, not in
        // tick, to keep the shared-cache writes off the concurrent path (a
        // TSan concern) — the per-chunk pending bits are bit-packed, so a worker
        // thread writing them would be a genuine race, not a benign same-value
        // store.
        for (std::vector<PendingRange> &worker : pendingByWorker_) {
            for (const PendingRange &range : worker) {
                if (range.upload_) {
                    range.pool_->queuePositionRange(range.startIdx_, range.count_);
                } else {
                    range.pool_->markCullBoundsDirty(range.startIdx_, range.count_);
                }
            }
        }
    }

    static SystemId create() {
        // MainThread: the tick drives IRJob::parallelFor itself, which
        // cannot fan out from a worker, so the system stays out of
        // multi-system groups.
        return registerSystem<
            UPDATE_VOXEL_SET_CHILDREN,
            C_VoxelSetNew,
            const C_WorldTransform,
            Exclude<C_Settled>,
            MainThread>("UpdateVoxelSetChildren");
    }
};
} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */

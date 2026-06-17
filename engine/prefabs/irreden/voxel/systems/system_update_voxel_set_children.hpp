#ifndef SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H
#define SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/components/component_player.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/sun_shadow_constants.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {
template <> struct System<UPDATE_VOXEL_SET_CHILDREN> {
    // Per-tick scratch: last-resolved canvas → pool pointer. The pool is
    // fetched fresh each tick (not across frames) because a between-frame
    // canvas archetype migration invalidates the pointer; within a single
    // tick the archetype is stable so amortizing the lookup is safe.
    IREntity::EntityId lastCanvas_ = IREntity::kNullEntity;
    C_VoxelPool *lastPool_ = nullptr;

    // Cull viewport captured from the previous render frame (one-frame lag,
    // consistent with the GPU cull and invisible for camera pans). cullValid_
    // is false until the render pipeline produces a non-zero canvas size.
    bool cullValid_ = false;
    IsoBounds2D viewport_ = {};

    void beginTick() {
        lastCanvas_ = IREntity::kNullEntity;
        lastPool_ = nullptr;

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

        IREntity::EntityId canvas = voxelSet.canvasEntity_;
        if (canvas == IREntity::kNullEntity) {
            canvas = IRRender::getActiveCanvasEntity();
        }
        if (canvas != lastCanvas_ || lastPool_ == nullptr) {
            lastPool_ = &IREntity::getComponent<C_VoxelPool>(canvas);
            lastCanvas_ = canvas;
        }
        C_VoxelPool &pool = *lastPool_;

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

        // updateAsChild returns the number of positions actually written
        // (may be < numVoxels_ if the pool-bounds guard fires). Use the
        // exact count to avoid queuing stale tail slots for GPU upload.
        const int writtenCount = voxelSet.updateAsChild(
            worldTransform.translation_,
            pool.getPositionGlobals(),
            pool.getPositions(),
            pool.getPositionOffsets()
        );
        if (writtenCount > 0) {
            // CPU world positions moved in place; evict the chunk world-AABB
            // cache so the continuous-yaw cull re-derives this set's bounds and
            // stays a conservative superset of the live voxels (#1439).
            pool.markChunkWorldBoundsDirty();
        }
        // A GPU-transform-indirected set (#1396) has binding 5 written by the
        // UPDATE_VOXEL_POSITIONS_GPU prepass each frame. We still recompute its
        // CPU global mirror above (a sane translation-only fallback for the
        // STAGE_1 canvas-switch re-seed, and for cull/picking), but we must NOT
        // queue it for the steady-state binding-5 flush — that flush runs after
        // the prepass in the RENDER pipeline and would clobber the GPU positions.
        if (writtenCount > 0 && voxelSet.gpuTransformSlot_ == IRRender::kVoxelTransformStatic) {
            pool.queuePositionRange(voxelSet.voxelStartIdx_, static_cast<size_t>(writtenCount));
        }
        if (voxelSet.ownerEntityId_ == IREntity::kNullEntity && entityId != IREntity::kNullEntity &&
            voxelSet.numVoxels_ > 0) {
            voxelSet.ownerEntityId_ = entityId;
            pool.setEntityIdForRange(
                voxelSet.voxelStartIdx_,
                static_cast<size_t>(voxelSet.numVoxels_),
                entityId
            );
        }
    }

    // PARALLELIZATION (#1803) — DESIGN-BLOCKED. The 262K-entity position
    // loop above is one of the two dominant UPDATE costs (#1740) and the
    // plan (.fleet/plans/issue-1803.md) calls for `Concurrency::PARALLEL_FOR`.
    // Two architecture-shaped blockers the plan's hazard list missed must be
    // resolved before the tag can land (see the PR's ## NEEDS-DESIGN comment):
    //
    //   1. EntityId ownership registration vs the PARALLEL_FOR validator.
    //      This tick uses the per-entity-id form to do the one-time owner
    //      registration (`pool.setEntityIdForRange` → GPU picking buffer +
    //      the `ownerEntityId_` guard). `ir_system.hpp` makes
    //      `PARALLEL_FOR + usesEntityId_ + !parallelSafe_` FATAL. The write
    //      is genuinely thread-safe (disjoint span fill), so `ParallelSafe`
    //      is the sanctioned tag — but `ParallelSafe` is currently unwired:
    //      `PartitionExcludes` (archetype) and `MakeMemberTickFn` (tick
    //      lambda) strip only `Exclude<...>`, not tag types. The latent
    //      wiring needs completing (reuse `detail::FilterTags_t`) OR the
    //      registration must be relocated out of the parallel tick.
    //   2. Pool resolution runs `getComponent<C_VoxelPool>(canvas)` on the
    //      worker (foreign-entity lookup) — not worker-safe per
    //      `engine/system/CLAUDE.md`. Plus `lastCanvas_`/`lastPool_` race as
    //      System<N> members under PARALLEL_FOR. Resolution must move to a
    //      main-thread `beginTick` canvas→pool pre-resolve.
    //
    // The thread_local pending-range merge the plan describes is the easy
    // part and lands once (1) and (2) are settled.
    static SystemId create() {
        return registerSystem<UPDATE_VOXEL_SET_CHILDREN, C_VoxelSetNew, C_WorldTransform>(
            "UpdateVoxelSetChildren"
        );
    }
};
} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */

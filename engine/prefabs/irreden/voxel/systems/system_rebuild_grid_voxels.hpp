#ifndef SYSTEM_REBUILD_GRID_VOXELS_H
#define SYSTEM_REBUILD_GRID_VOXELS_H

// SYSTEM_REBUILD_GRID_VOXELS — Epic C, C6 (T-294).
//
// Re-rasterizes a GRID-mode entity's authored voxels into rotated world
// cells. Runs AFTER UPDATE_VOXEL_SET_CHILDREN in the UPDATE pipeline — the
// translate-only path writes a baseline, this system overwrites with the
// entity's full SQT (rotation/scale composed into world cells).
//
// On-screen entities re-rasterize unconditionally each frame (assume
// dynamic). The only work we skip is for entities the camera can't see —
// a cull concern, not a "did the transform change" concern. The system
// queries the canvas voxel pool's per-chunk iso bounds (the same data the
// GPU chunk-visibility mask reads) and skips a voxel set whose pool chunks
// are all outside the cull viewport. There is deliberately NO per-set
// snapshot-compare early-out: a "did this change since last frame"
// side-channel is a dirty flag in disguise (see `cpp-ecs.md` "No dirty
// flags").
//
// Cell aliasing is accepted by design: multiple authored voxels may
// collapse into the same world cell after rounding (e.g. a 45° Z-rotated
// cube). Visual collisions resolve in render-pipeline iteration order,
// which is deterministic across frames given stable archetype layout
// and entity ids — so the "winner" at a contested cell is consistent
// frame-to-frame even though it is not literally sorted by entity id.
//
// Skipped (early returns):
//  - `C_RotationMode::mode_ != GRID` — DETACHED entities rotate through
//    the per-canvas TRS composite (system_entity_canvas_to_framebuffer)
//    and never touch the world voxel pool's positions.
//  - `numVoxels_ <= 0` — headless / pre-canvas staging.
//  - The voxel set's pool chunks are all outside the cull viewport.

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/sun_shadow_constants.hpp>
#include <irreden/render/voxel_pool_allocation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/grid_rotation.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {
template <> struct System<REBUILD_GRID_VOXELS> {
    // Per-tick scratch: last-resolved canvas → pool pointer. Mirrors the
    // same pattern used by UPDATE_VOXEL_SET_CHILDREN — the pool is
    // refetched fresh each tick (canvas archetype migrations between
    // frames invalidate the pointer; within a tick the archetype is
    // stable so amortizing the lookup across multiple voxel-set entities
    // on the same canvas is safe).
    IREntity::EntityId lastCanvas_ = IREntity::kNullEntity;
    C_VoxelPool *lastPool_ = nullptr;

    // Frame-scoped cull viewport, resolved once in beginTick. `cullValid_`
    // is false on the very first frame (before any render populates the
    // cull viewport) — the gate then treats every set as visible.
    IsoBounds2D chunkVp_{};
    bool cullValid_ = false;

    void beginTick() {
        lastCanvas_ = IREntity::kNullEntity;
        lastPool_ = nullptr;

        // Resolve the cull viewport once per frame, mirroring the chunk
        // visibility gate in system_voxel_to_trixel.hpp. This system runs
        // in the UPDATE pipeline, so getCullViewport() holds the previous
        // frame's render-event snapshot — a one-frame lag that stays
        // consistent with the GPU cull and is invisible for camera pans.
        const IRRender::CullViewportState &cull = IRRender::getCullViewport();
        cullValid_ = cull.canvasSize_.x > 0 && cull.canvasSize_.y > 0;
        if (cullValid_) {
            chunkVp_ = IRPrefab::SunShadow::shadowFeederCullViewport(
                IRRender::kCullChunkMargin,
                IRPrefab::SunShadow::frameShadowFeederParams(),
                cull
            );
        }
    }

    void tick(
        C_VoxelSetNew &voxelSet,
        const C_WorldTransform &worldTransform,
        const C_RotationMode &rotationMode
    ) {
        if (rotationMode.mode_ != RotationMode::GRID) {
            return;
        }
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

        // Cull gate: skip re-rasterization when none of this set's pool
        // chunks are visible. Visible sets re-rasterize every frame.
        if (cullValid_ && !pool.isRangeVisible(
                              voxelSet.voxelStartIdx_,
                              static_cast<std::size_t>(voxelSet.numVoxels_),
                              chunkVp_
                          )) {
            return;
        }

        const std::vector<IRRender::VoxelGpuPosition> &poolPositions = pool.getPositions();
        const std::vector<vec3> &poolOffsets = pool.getPositionOffsets();
        std::vector<IRRender::VoxelGpuPosition> &poolGlobals = pool.getPositionGlobals();

        const size_t baseIdx = voxelSet.voxelStartIdx_;
        const size_t availPositions =
            poolPositions.size() > baseIdx ? poolPositions.size() - baseIdx : 0u;
        const size_t availOffsets =
            poolOffsets.size() > baseIdx ? poolOffsets.size() - baseIdx : 0u;
        const size_t availGlobals =
            poolGlobals.size() > baseIdx ? poolGlobals.size() - baseIdx : 0u;
        const int safeCount = IRMath::min(
            voxelSet.numVoxels_,
            static_cast<int>(IRMath::min(IRMath::min(availPositions, availOffsets), availGlobals))
        );
        for (int i = 0; i < safeCount; ++i) {
            poolGlobals[baseIdx + i].pos_ = IRPrefab::GridRotation::worldCellForGridVoxel(
                poolPositions[baseIdx + i].pos_,
                poolOffsets[baseIdx + i],
                worldTransform
            );
        }
    }

    static SystemId create() {
        return registerSystem<REBUILD_GRID_VOXELS, C_VoxelSetNew, C_WorldTransform, C_RotationMode>(
            "RebuildGridVoxels"
        );
    }
};
} // namespace IRSystem

#endif /* SYSTEM_REBUILD_GRID_VOXELS_H */

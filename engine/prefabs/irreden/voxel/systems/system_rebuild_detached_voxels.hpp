#ifndef SYSTEM_REBUILD_DETACHED_VOXELS_H
#define SYSTEM_REBUILD_DETACHED_VOXELS_H

// SYSTEM_REBUILD_DETACHED_VOXELS — detached re-voxelize, #1553 epic P1 (#1555).
//
// The detached analogue of SYSTEM_REBUILD_GRID_VOXELS. For a
// RotationMode::DETACHED_REVOXELIZE entity, re-rasterizes the private
// per-canvas voxel pool into the entity's full-rotation CELL positions each
// frame, so the canvas can render its pool through CARDINAL/static frame data
// (VOXEL_TO_TRIXEL_STAGE_1's re-voxelize branch). The rotation lives in the
// cells, not a 2D forward-scatter deform — the model the per-axis path cannot
// represent for an asymmetric solid (#1551 root cause).
//
// Ticks the CANVAS entity (it owns both the private pool and the
// camera-composed rotation): PROPAGATE_CANVAS_ROTATION wrote
// `quatInverse(R_camera) * entityRotation` into `C_CanvasLocalRotation.rotation_`
// and set `reVoxelize_` true. Register in the UPDATE pipeline AFTER
// PROPAGATE_CANVAS_ROTATION (so the rotation is current) and after
// UPDATE_VOXEL_SET_CHILDREN (its translate-only baseline is overwritten here);
// it must run before the RENDER pipeline reads the pool.
//
// Two departures from the GRID sibling, both deliberate:
//  - Rotation is applied about the pool ORIGIN with zero translation. The
//    detached demo authors its solids centered around origin
//    (`C_VoxelSetNew(..., centerAroundOrigin=true, ...)`), and screen placement
//    is owned by the composite (ENTITY_CANVAS_TO_FRAMEBUFFER) — so the canvas's
//    pool stays canvas-local, never world-translated.
//  - No frustum-cull gate. The world cull viewport is in world iso space; a
//    detached canvas's private pool lives in canvas-local space, so the GRID
//    gate would mis-cull it. Detached pools are small and always re-filled.
//
// INVARIANT: one centered voxel set per private pool. Unlike the GRID sibling
// (which ticks per voxel-SET and rewrites only that set's span with its own
// C_WorldTransform), this ticks the CANVAS and rewrites the WHOLE pool
// [0, safeCount) with the single C_CanvasLocalRotation.rotation_ about the pool
// origin. A second set added to the same detached canvas would therefore be
// rotated about the canvas origin and ignore its own local transform. The
// current consumer (one createWithVoxelPool solid) is correct; a future
// multi-set detached canvas must rotate per set about each set's pivot. See
// voxel/CLAUDE.md.
//
// Skipped (early returns):
//  - `C_CanvasLocalRotation::reVoxelize_ == false` — the main world canvas
//    (sentinel rotation) and forward-scatter DETACHED canvases.
//  - empty pool.

#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/grid_rotation.hpp>

namespace IRSystem {

template <> struct System<REBUILD_DETACHED_VOXELS> {
    void tick(
        IRComponents::C_VoxelPool &pool, const IRComponents::C_CanvasLocalRotation &canvasRotation
    ) {
        if (!canvasRotation.reVoxelize_) {
            return;
        }
        const int liveCount = pool.getLiveVoxelCount();
        if (liveCount <= 0) {
            return;
        }

        const std::vector<IRRender::VoxelGpuPosition> &localPositions = pool.getPositions();
        const std::vector<IRMath::vec3> &localOffsets = pool.getPositionOffsets();
        std::vector<IRRender::VoxelGpuPosition> &globals = pool.getPositionGlobals();

        const int safeCount = IRMath::min(
            liveCount,
            static_cast<int>(
                IRMath::min(IRMath::min(localPositions.size(), localOffsets.size()), globals.size())
            )
        );
        if (safeCount <= 0) {
            return;
        }

        // Rotate the centered authored voxels about the pool origin (scale 1,
        // no translation). worldCellForGridVoxel handles the scale → rotate →
        // round chain and the identity fast-path, so identity rotations leave
        // the cardinal positions intact. Reuses the GRID transform math — no
        // duplicate rotation code (#1553 plan).
        const IRComponents::C_WorldTransform localRotationOnly{
            IRMath::vec3(0.0f),
            canvasRotation.rotation_,
            IRMath::vec3(1.0f)
        };
        for (int i = 0; i < safeCount; ++i) {
            globals[i].pos_ = IRPrefab::GridRotation::worldCellForGridVoxel(
                localPositions[i].pos_,
                localOffsets[i],
                localRotationOnly
            );
        }
        // The chunk world-AABB cache must follow the rewritten positions, or a
        // stale (pre-rotation) box could drop chunks under the visibility mask
        // (mirrors SYSTEM_REBUILD_GRID_VOXELS, #1439).
        pool.markChunkWorldBoundsDirty();
    }

    static SystemId create() {
        return registerSystem<
            REBUILD_DETACHED_VOXELS,
            IRComponents::C_VoxelPool,
            IRComponents::C_CanvasLocalRotation>("RebuildDetachedVoxels");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_REBUILD_DETACHED_VOXELS_H */

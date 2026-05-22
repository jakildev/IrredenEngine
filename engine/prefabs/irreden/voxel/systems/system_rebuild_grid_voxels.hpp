#ifndef SYSTEM_REBUILD_GRID_VOXELS_H
#define SYSTEM_REBUILD_GRID_VOXELS_H

// SYSTEM_REBUILD_GRID_VOXELS — Epic C, C6 (T-294).
//
// Re-rasterizes a GRID-mode entity's authored voxels into rotated world
// cells whenever its `C_WorldTransform` changes. Runs AFTER
// UPDATE_VOXEL_SET_CHILDREN in the UPDATE pipeline — the translate-only
// path writes a baseline, this system overwrites for entities whose SQT
// rotation/scale (or world translation) shifted relative to the cached
// snapshot on the voxel set.
//
// Push-at-mutation: the snapshot lives in `C_VoxelSetNew::lastRebuild*_`
// (component fields rather than a dirty flag — see `cpp-ecs.md`
// "No dirty flags"). When all three components of the world transform
// match the cache, the tick is a O(1) early return.
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
//  - Cached `lastRebuildWorldTransform_*` matches live world transform.

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
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

    void beginTick() {
        lastCanvas_ = IREntity::kNullEntity;
        lastPool_ = nullptr;
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

        const bool unchanged = voxelSet.hasLastRebuildWorldTransform_ &&
                               voxelSet.lastRebuildWorldRotation_ == worldTransform.rotation_ &&
                               voxelSet.lastRebuildWorldScale_ == worldTransform.scale_ &&
                               voxelSet.lastRebuildWorldTranslation_ == worldTransform.translation_;
        if (unchanged) {
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

        voxelSet.lastRebuildWorldRotation_ = worldTransform.rotation_;
        voxelSet.lastRebuildWorldScale_ = worldTransform.scale_;
        voxelSet.lastRebuildWorldTranslation_ = worldTransform.translation_;
        voxelSet.hasLastRebuildWorldTransform_ = true;
    }

    static SystemId create() {
        return registerSystem<REBUILD_GRID_VOXELS, C_VoxelSetNew, C_WorldTransform, C_RotationMode>(
            "RebuildGridVoxels"
        );
    }
};
} // namespace IRSystem

#endif /* SYSTEM_REBUILD_GRID_VOXELS_H */

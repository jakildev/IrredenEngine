#ifndef SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H
#define SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_player.hpp>
#include <irreden/ir_render.hpp>

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

    void beginTick() {
        lastCanvas_ = IREntity::kNullEntity;
        lastPool_ = nullptr;
    }

    void tick(IREntity::EntityId &entityId, C_VoxelSetNew &voxelSet, C_PositionGlobal3D &position) {
        IREntity::EntityId canvas = voxelSet.canvasEntity_;
        if (canvas == IREntity::kNullEntity) {
            canvas = IRRender::getActiveCanvasEntity();
        }
        if (canvas != lastCanvas_ || lastPool_ == nullptr) {
            lastPool_ = &IREntity::getComponent<C_VoxelPool>(canvas);
            lastCanvas_ = canvas;
        }
        C_VoxelPool &pool = *lastPool_;
        // updateAsChild returns the number of positions written, or 0 if the
        // parent is unchanged (static voxel scene pays zero bytes/frame on
        // the GPU side). Using the exact count avoids queuing stale tail
        // slots if the pool-bounds guard fires (safeCount < numVoxels_).
        const int writtenCount = voxelSet.updateAsChild(
            position.pos_,
            pool.getPositionGlobals(),
            pool.getPositions(),
            pool.getPositionOffsets()
        );
        if (writtenCount > 0) {
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

    static SystemId create() {
        return registerSystem<UPDATE_VOXEL_SET_CHILDREN, C_VoxelSetNew, C_PositionGlobal3D>(
            "UpdateVoxelSetChildren"
        );
    }
};
} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */

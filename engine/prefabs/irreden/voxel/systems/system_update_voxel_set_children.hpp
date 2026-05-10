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
    static SystemId create() {
        return createSystem<C_VoxelSetNew, C_PositionGlobal3D>(
            "UpdateVoxelSetChildren",
            [](IREntity::EntityId &entityId,
               C_VoxelSetNew &voxelSet,
               C_PositionGlobal3D &position) {
                voxelSet.updateAsChild(position.pos_);

                if (voxelSet.ownerEntityId_ == IREntity::kNullEntity &&
                    entityId != IREntity::kNullEntity && voxelSet.numVoxels_ > 0) {
                    voxelSet.ownerEntityId_ = entityId;
                    // The pool pointer is fetched fresh per call rather than
                    // cached: a canvas archetype mutation invalidates any
                    // cached @c C_VoxelPool* and a subsequent pointer-diff
                    // against `voxelSet.globalPositions_.data()` then yields
                    // a wild index. The gate above keeps this getComponent a
                    // one-shot per voxel-set lifetime.
                    IREntity::EntityId canvas = voxelSet.canvasEntity_;
                    if (canvas == IREntity::kNullEntity) {
                        canvas = IRRender::getActiveCanvasEntity();
                    }
                    IREntity::getComponent<C_VoxelPool>(canvas).setEntityIdForRange(
                        voxelSet.voxelStartIdx_,
                        static_cast<size_t>(voxelSet.numVoxels_),
                        entityId
                    );
                }
            }
        );
    }
};
} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */

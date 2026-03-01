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
                    entityId != IREntity::kNullEntity &&
                    voxelSet.numVoxels_ > 0) {
                    voxelSet.ownerEntityId_ = entityId;
                    IREntity::EntityId canvasEntity = IRRender::getCanvas("main");
                    auto &pool = IREntity::getComponent<C_VoxelPool>(canvasEntity);
                    size_t startIdx = voxelSet.globalPositions_.data() -
                                      pool.getPositionGlobalsBasePtr();
                    pool.setEntityIdForRange(startIdx, voxelSet.numVoxels_, entityId);
                    IRE_LOG_INFO(
                        "[VoxelSetChildren] Set entityId={} for {} voxels at poolIdx={}",
                        entityId, voxelSet.numVoxels_, startIdx
                    );
                }
            }
        );
    }
};
} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */

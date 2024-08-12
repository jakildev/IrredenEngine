/*
 * Project: Irreden Engine
 * File: system_update_voxel_set_children.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H
#define SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_player.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {
    template<>
    struct System<UPDATE_VOXEL_SET_CHILDREN>  {
        static SystemId create() {
            return createSystem<C_VoxelSetNew, C_PositionGlobal3D>(
                "UpdateVoxelSetChildren",
                [](
                    C_VoxelSetNew& voxelSet,
                    C_PositionGlobal3D& position
                )
                {
                    voxelSet.updateAsChild(
                        position.pos_
                    );
                }
            );
        }

    };
} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */

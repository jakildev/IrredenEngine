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

#include <irreden/ir_ecs.hpp>

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_player.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {
    template<>
    class System<UPDATE_VOXEL_SET_CHILDREN> : public SystemBase<
        UPDATE_VOXEL_SET_CHILDREN,
        C_PositionGlobal3D,
        C_VoxelSetNew
    >   {
    public:
        System()
        // :   m_voxelScene{}
        {
            IRProfile::engLogInfo("Created system UPDATE_VOXEL_SET_CHILDREN");
        }

        virtual ~System() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities,
            std::vector<C_PositionGlobal3D>& positions, // change to global position
            std::vector<C_VoxelSetNew>& voxelSets // is a voxel set a top level only?
        )
        {
            const bool isPlayer = archetype.contains(IRECS::getEntityManager().getComponentType<C_Player>());
            for(int i=0; i < entities.size(); i++) {
                if(isPlayer) {
                    voxelSets[i].updateAsChild(
                        vec3(0, 0, 0)
                    );
                    continue;
                }
                voxelSets[i].updateAsChild(
                    positions[i].pos_
                );
            }
        }

    private:
        virtual void beginExecute() override {
            // THIS MUST CHANGE, WAY TOO SLOW
            // m_voxelScene.update();
        }
        virtual void endExecute() override {}
    };
} // namespace IRECS

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */

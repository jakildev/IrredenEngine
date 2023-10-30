/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_update_voxel_set_children.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H
#define SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H

#include <irreden/ecs/ir_system_base.hpp>

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_player.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {
    template<>
    class IRSystem<UPDATE_VOXEL_SET_CHILDREN> : public IRSystemBase<
        UPDATE_VOXEL_SET_CHILDREN,
        C_PositionGlobal3D,
        C_VoxelSetNew
    >   {
    public:
        IRSystem()
        :   m_voxelScene{}
        {
            IRProfile::engLogInfo("Created system UPDATE_VOXEL_SET_CHILDREN");
        }

        virtual ~IRSystem() = default;

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

        void addEntityToScene(
            EntityHandle voxelSet,
            EntityHandle parent = EntityHandle{0}
        )
        {
            // Make sure it has a global position
            voxelSet.set(C_PositionGlobal3D{});
            m_voxelScene.addNode(voxelSet, parent);
        }

        void removeEntityFromScene(
            EntityHandle voxelSet
        )
        {
            m_voxelScene.removeNode(voxelSet);
        }

    private:
        IRComponents::C_VoxelScene m_voxelScene;
        virtual void beginExecute() override {
            // THIS MUST CHANGE, WAY TOO SLOW
            m_voxelScene.update();
        }
        virtual void endExecute() override {}
    };
} // namespace IRECS

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */

/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_voxel_scene.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Unused right now but might be useful later

#ifndef SYSTEM_VOXEL_SCENE_H
#define SYSTEM_VOXEL_SCENE_H

#include "..\ecs\ir_system_base.hpp"

#include "..\components\component_voxel_set.hpp"
#include "..\components\component_position_3d.hpp"
#include "..\components\component_position_global_3D.hpp"

using namespace IRComponents;
using namespace IRMath;

// Scene node entities must be sorted in depth first order
// that is how update algorithm will work...
namespace IRECS {
    template<>
    class IRSystem<VOXEL_SCENE> : public IRSystemBase<
        VOXEL_SCENE,
        C_Position3D,
        C_PositionGlobal3D,
        C_VoxelSceneNode
    >   {
    public:
        IRSystem() {
            ENG_LOG_INFO("Created system VOXEL_SCENE");
        }
        virtual ~IRSystem() = default;

        // WIP WIP WIP LEFT OFF HERE WIP WIP WIP
        // simply a matter of updating positions for now based
        // on a depth first traversal alg in a flat array...
        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities,
            std::vector<C_Position3D>& positions,
            std::vector<C_PositionGlobal3D> positionsGlobal,
            std::vector<C_VoxelSceneNode>& voxelSceneNodes
        )
        {
            EASY_BLOCK("IRSystem<VOXEL_SCENE>::tickWithArchetype");
            for(int i=0; i < entities.size(); i++) {
                voxelSets[i].updateChildren(
                    positions[i].pos_
                );
            }
        }

        // void tickWithArchetypeAlt(
        //     Archetype archetype,
        //     std::vector<EntityId>& entities,
        //     std::vector<C_Position3D>& positions,
        //     std::vector<C_PositionOffset3D>& positionOffsets,
        //     std::vector<C_VoxelSetNew>& voxelSets
        // )
        // {
        //     EASY_BLOCK("IRSystem<VOXEL_SCENE>::tickWithArchetype");
        //     for(int i=0; i < entities.size(); i++) {
        //         voxelSets[i].updateChildren(
        //             positions[i].pos_ + positionOffsets[i].pos_
        //         );
        //     }
        // }
    private:
        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };
} // namespace IRECS

#endif /* SYSTEM_VOXEL_SCENE_H */

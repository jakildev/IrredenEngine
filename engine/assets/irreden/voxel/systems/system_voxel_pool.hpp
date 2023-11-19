/*
 * Project: Irreden Engine
 * File: system_voxel_pool.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Must be extended to sort heirarchies and the like

#ifndef SYSTEM_VOXEL_POOL_H
#define SYSTEM_VOXEL_POOL_H

#include <irreden/ir_ecs.hpp>

#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/entities/entity_single_voxel.hpp>
#include <irreden/update/components/component_lifetime.hpp>

#include <queue>
#include <span>
#include <map>
#include <set>
#include <optional>

// using namespace IRECS;
using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

    // A voxel pool is a voxel scene
    template<>
    class System<VOXEL_POOL> : public SystemBase<
        VOXEL_POOL
    >   {
    public:
        System(ivec3 numVoxelsMainCanvas, ivec3 numVoxelsPlayer)
        {
            m_voxelPoolIdMainCanvas = m_voxelPools.size();
            m_voxelPools.push_back(C_VoxelPool(numVoxelsMainCanvas));
            m_voxelPoolIdPlayer = m_voxelPools.size();
            m_voxelPools.push_back(C_VoxelPool(numVoxelsPlayer));
            IRProfile::engLogInfo("Created system VOXEL_POOL");
        }

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities
        )
        {

        }

    private:
        std::vector<EntityId> m_voxelPoolEntities;
        std::vector<C_VoxelPool> m_voxelPools;
        int m_voxelPoolIdMainCanvas;
        int m_voxelPoolIdPlayer;

        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };

}

#endif /* SYSTEM_VOXEL_POOL_H */

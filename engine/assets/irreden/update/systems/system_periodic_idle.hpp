/*
 * Project: Irreden Engine
 * File: system_periodic_idle.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_PERIODIC_IDLE_H
#define SYSTEM_PERIODIC_IDLE_H

#include <irreden/system/ir_system_base.hpp>

#include "..\components\component_periodic_idle.hpp"
#include <irreden/voxel/components/component_voxel_set.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    class System<PERIODIC_IDLE> : public SystemBase<
        PERIODIC_IDLE,
        C_PeriodicIdle,
        C_VoxelSetNew
    >   {
    public:
        System()
        {
            IRProfile::engLogInfo("Created system PERIODIC_IDLE");
        }
        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities,
            std::vector<C_PeriodicIdle>& perodicIdles,
            std::vector<C_VoxelSetNew>& voxelSets
        )
        {
            for(int i=0; i < entities.size(); i++) {
                perodicIdles[i].tick();
                for(int j=0; j < voxelSets[i].positionOffsets_.size(); j++) {
                    voxelSets[i].positionOffsets_[j] =
                        vec3(0.0f, 0.0f, perodicIdles[i].getValue());
                }
            }
        }
    private:
        virtual void beginExecute() override {}

        virtual void endExecute() override {}

    };


} // namespace System

#endif /* SYSTEM_PERIODIC_IDLE_H */

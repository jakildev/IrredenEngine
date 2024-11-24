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

#include <irreden/ir_system.hpp>

#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

    template<>
    struct System<PERIODIC_IDLE> {
        static SystemId create() {
            // TODO: refact this, shouldnt update voxel sets directly
            return createSystem<C_PeriodicIdle, C_VoxelSetNew>(
                "PeriodicIdle",
                [](
                    C_PeriodicIdle& periodicIdle,
                    C_VoxelSetNew& voxelSet
                )
                {
                    periodicIdle.tick();
                    for(int i=0; i < voxelSet.positionOffsets_.size(); i++) {
                        voxelSet.positionOffsets_[i] =
                            vec3(0.0f, 0.0f, periodicIdle.getValue());
                    }
                }
            );
        }
    };

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_H */

#ifndef SYSTEM_PERIODIC_IDLE_NOTE_BURST_H
#define SYSTEM_PERIODIC_IDLE_NOTE_BURST_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/update/components/component_particle_burst.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_velocity_drag.hpp>
#include <irreden/update/components/component_lifetime.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<PERIODIC_IDLE_NOTE_BURST> {
    static SystemId create() {
        return createSystem<C_PeriodicIdle, C_ParticleBurst, C_VoxelSetNew, C_PositionGlobal3D>(
            "PeriodicIdleNoteBurst",
            [](C_PeriodicIdle &idle, C_ParticleBurst &burst, C_VoxelSetNew &voxelSet,
               C_PositionGlobal3D &globalPos) {
                if (!idle.cycleCompleted_) {
                    return;
                }
                Color color = voxelSet.voxels_[0].color_;
                float spd = burst.speed_;
                for (int i = 0; i < burst.count_; i++) {
                    // Wide horizontal spread + upward launch for impact-splash feel
                    vec3 vel = randomVec(vec3(-spd * 2.0f, -spd * 2.0f, spd * 0.2f),
                                         vec3(spd * 2.0f, spd * 2.0f, spd * 1.5f));
                    IREntity::createEntity(C_Position3D{globalPos.pos_},
                                           C_VoxelSetNew{ivec3(1, 1, 1), color},
                                           C_Velocity3D{vel}, C_VelocityDrag{},
                                           C_Lifetime{burst.lifetime_});
                }
            });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_NOTE_BURST_H */

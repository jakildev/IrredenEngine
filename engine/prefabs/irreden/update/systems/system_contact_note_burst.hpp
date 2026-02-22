#ifndef SYSTEM_CONTACT_NOTE_BURST_H
#define SYSTEM_CONTACT_NOTE_BURST_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/update/components/component_contact_event.hpp>
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

template <> struct System<CONTACT_NOTE_BURST> {
    static SystemId create() {
        return createSystem<C_ContactEvent, C_ParticleBurst, C_VoxelSetNew, C_PositionGlobal3D>(
            "ContactNoteBurst",
            [](const C_ContactEvent &contact,
               C_ParticleBurst &burst,
               C_VoxelSetNew &voxelSet,
               C_PositionGlobal3D &globalPos) {
                if (!contact.entered_) {
                    return;
                }

                Color color = voxelSet.voxels_[0].color_;
                float spd = burst.speed_;
                for (int i = 0; i < burst.count_; i++) {
                    vec3 vel = randomVec(
                        vec3(-spd * 2.0f, -spd * 2.0f, -spd * 1.2f),
                        vec3(spd * 2.0f, spd * 2.0f, spd * 0.8f)
                    );
                    IREntity::EntityId entity = IREntity::createEntity(
                        C_Position3D{globalPos.pos_},
                        C_VoxelSetNew{ivec3(1, 1, 1), color},
                        C_Velocity3D{vel},
                        C_VelocityDrag{},
                        C_Lifetime{burst.lifetime_}
                    );
                    // New particles spawn after GLOBAL_POSITION_3D in this frame.
                    // Seed global position immediately to avoid a one-frame origin artifact.
                    IREntity::setComponent(entity, C_PositionGlobal3D{globalPos.pos_});
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CONTACT_NOTE_BURST_H */

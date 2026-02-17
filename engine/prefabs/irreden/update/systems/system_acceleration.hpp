#ifndef SYSTEM_ACCELERATION_H
#define SYSTEM_ACCELERATION_H

#include <irreden/ir_ecs.hpp>

#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>

using namespace IRComponents;

namespace IRECS {

template <> struct System<ACCELERATION_3D> {
    static SystemId create() {
        return createSystem<C_Velocity3D, C_Acceleration3D>(
            "Acceleration3D", [](C_Velocity3D &velocity, const C_Acceleration3D &acceleration) {
                velocity.velocity_ +=
                    acceleration.acceleration_ * vec3(IRTime::deltaTime(IRTime::UPDATE));
            });
    }
};

} // namespace IRECS

#endif /* SYSTEM_ACCELERATION_H */

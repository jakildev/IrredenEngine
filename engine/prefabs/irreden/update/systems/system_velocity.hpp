#ifndef SYSTEM_VELOCITY_H
#define SYSTEM_VELOCITY_H

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<VELOCITY_3D> {
    static SystemId create() {
        return createSystem<C_LocalTransform, C_Velocity3D>(
            "Velocity3D",
            [](C_LocalTransform &localXform, const C_Velocity3D &velocity) {
                localXform.translation_ +=
                    velocity.velocity_ * vec3(IRTime::deltaTime(IRTime::UPDATE));
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VELOCITY_H */

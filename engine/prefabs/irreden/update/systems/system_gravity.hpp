#ifndef SYSTEM_GRAVITY_H
#define SYSTEM_GRAVITY_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_gravity_3d.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<GRAVITY_3D> {
    static C_Gravity3D &gravity() {
        static C_Gravity3D instance{};
        return instance;
    }

    static SystemId create() {
        SystemId system = createSystem<C_Velocity3D>(
            "Gravity3D",
            [](C_Velocity3D &velocity) {
                velocity.velocity_ +=
                    gravity().getVector() * vec3(IRTime::deltaTime(IRTime::UPDATE));
            }
        );
        IRSystem::addSystemTag<C_HasGravity>(system);
        return system;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GRAVITY_H */

/*
 * Project: Irreden Engine
 * File: system_gravity.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_GRAVITY_H
#define SYSTEM_GRAVITY_H

#include <irreden/ir_ecs.hpp>

#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_gravity_3d.hpp>

using namespace IRComponents;

namespace IRECS {

    template<>
    struct System<GRAVITY_3D> {
        static SystemId create() {
            static C_Gravity3D gravity = C_Gravity3D{};
            SystemId system =  createSystem<C_Velocity3D>(
                "Gravity3D",
                [](
                    C_Velocity3D& velocity
                )
                {
                    velocity.velocity_ += gravity.direction_.direction_;
                }
            );
            IRECS::addSystemTag<C_HasGravity>(system);
            return system;
        }
    };

} // namespace IRECS

#endif /* SYSTEM_GRAVITY_H */

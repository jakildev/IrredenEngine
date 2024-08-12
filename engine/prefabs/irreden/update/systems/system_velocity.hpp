/*
 * Project: Irreden Engine
 * File: system_velocity.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_VELOCITY_H
#define SYSTEM_VELOCITY_H

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>

using namespace IRComponents;

namespace IRSystem {

    template<>
    struct System<VELOCITY_3D>{
        static SystemId create() {
            return createSystem<C_Position3D, C_Velocity3D>(
                "Velocity3D",
                [](
                    C_Position3D& position,
                    const C_Velocity3D& velocity
                )
                {
                    position.pos_ +=
                        // velocity.velocity_;
                        velocity.velocity_ *
                        vec3(IRTime::deltaTime(IRTime::UPDATE));
                }
            );
        }
    };

} // namespace IRSystem

#endif /* SYSTEM_VELOCITY_H */

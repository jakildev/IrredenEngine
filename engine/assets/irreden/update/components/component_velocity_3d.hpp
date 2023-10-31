/*
 * Project: Irreden Engine
 * File: component_velocity_3d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_VELOCITY_3D_H
#define COMPONENT_VELOCITY_3D_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;

namespace IRComponents {

    // Velocity is in blocks per second
    struct C_Velocity3D {
        vec3 velocity_;

        C_Velocity3D(vec3 velocity)
        :   velocity_(
                velocity /
                vec3(
                    IRConstants::kFPS,
                    IRConstants::kFPS,
                    IRConstants::kFPS
                )
            )
        {

        }

        C_Velocity3D(float x, float y, float z)
        :   C_Velocity3D(vec3{x, y, z})
        {

        }

        // Default
        C_Velocity3D()
        :   C_Velocity3D(vec3(0.0f))
        {

        }

        void tick() {

        }


    };

} // namespace IRComponents


#endif /* COMPONENT_VELOCITY_3D_H */

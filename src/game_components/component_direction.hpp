/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_direction.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_DIRECTION_H
#define COMPONENT_DIRECTION_H

#include "../math/ir_math.hpp"

namespace IRComponents {

    constexpr vec3 kDirecton3DUp{0, 0, -1};
    constexpr vec3 kDirecton3DDown{0, 0, 1};

    struct C_Direction3D {
        vec3 direction_;
        // float tempPackBuffer_;

        C_Direction3D(
            vec3 direction
        )
        :   direction_{IRMath::normalize(direction)}
        {

        }

        C_Direction3D(
            float x,
            float y,
            float z
        )
        :   C_Direction3D{vec3(x, y, z)}
        {

        }

        // Default
        C_Direction3D()
        :   C_Direction3D{vec3(0, 0, 1)}
        {

        }

        void set(vec3 direction) {
            direction_ = IRMath::normalize(direction);
        }
    };

} // namespace IRComponents

#endif /* COMPONENT_DIRECTION_H */

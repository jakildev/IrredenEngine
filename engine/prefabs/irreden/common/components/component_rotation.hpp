/*
 * Project: Irreden Engine
 * File: component_rotation.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_ROTATION_H
#define COMPONENT_ROTATION_H

#include <irreden/ir_math.hpp>

using IRMath::vec3;

struct C_Rotation {
    vec3 rot_;

    C_Rotation(
        vec3 rot
    )
    :   rot_{rot}
    {

    }

    C_Rotation(
        float x,
        float y,
        float z
    )
    :   C_Rotation{vec3(x, y, z)}
    {

    }

    C_Rotation()
    :   C_Rotation{vec3(0, 0, 0)}
    {

    }
}
#endif /* COMPONENT_ROTATION_H */

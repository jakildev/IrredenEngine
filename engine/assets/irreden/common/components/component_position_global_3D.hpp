/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_position_global_3D.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_POSITION_GLOBAL_3D_H
#define COMPONENT_POSITION_GLOBAL_3D_H

#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;

struct C_PositionGlobal3D {
    vec3 pos_;
    float tempPackBuffer_;

    C_PositionGlobal3D(
        vec3 pos
    )
    :   pos_{pos}
    {

    }

    C_PositionGlobal3D(
        float x,
        float y,
        float z
    )
    :   C_PositionGlobal3D{vec3(x, y, z)}
    {

    }

    // Default
    C_PositionGlobal3D()
    :   C_PositionGlobal3D{vec3(0, 0, 0)}
    {

    }
};

#endif /* COMPONENT_POSITION_GLOBAL_3D_H */

/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_position_int_3d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_POSITION_INT_3D_H
#define COMPONENT_POSITION_INT_3D_H

#include "../math/ir_math.hpp"

using IRMath::ivec3;

struct C_PositionInt3D {
    ivec3 pos_;

    C_PositionInt3D(ivec3 pos)
    :   pos_{pos}
    {

    }

    C_PositionInt3D(int x, int y, int z)
    :   C_PositionInt3D{ivec3(x, y, z)}
    {

    }

    // Default
    C_PositionInt3D()
    :   C_PositionInt3D{ivec3(0, 0, 0)}
    {

    }


};

#endif /* COMPONENT_POSITION_INT_3D_H */

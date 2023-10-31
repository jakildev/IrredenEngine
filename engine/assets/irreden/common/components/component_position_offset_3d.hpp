/*
 * Project: Irreden Engine
 * File: component_position_offset_3d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_POSITION_OFFSET_3D_H
#define COMPONENT_POSITION_OFFSET_3D_H

// Used for when you want to modify a entities position
// temporarily

#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_PositionOffset3D {
    vec3 pos_;
    float tempPackBuffer_;

    C_PositionOffset3D(
        vec3 pos
    )
    :   pos_{pos}
    {

    }

    C_PositionOffset3D(
        float x,
        float y,
        float z
    )
    :   C_PositionOffset3D{vec3(x, y, z)}
    {

    }

    // Default
    C_PositionOffset3D()
    :   C_PositionOffset3D{vec3(0, 0, 0)}
    {

    }


};

} // namespace IRComponents

#endif /* COMPONENT_POSITION_OFFSET_3D_H */

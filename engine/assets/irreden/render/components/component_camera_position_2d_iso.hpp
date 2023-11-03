/*
 * Project: Irreden Engine
 * File: component_camera_position_2d_iso.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_CAMERA_POSITION_2D_ISO_H
#define COMPONENT_CAMERA_POSITION_2D_ISO_H

#include <irreden/ir_math.hpp>

using  IRMath::vec2;

namespace IRComponents {


struct C_CameraPosition2DIso {
    vec2 pos_;
    float tempPackBuffer_;


    C_CameraPosition2DIso(
        vec2 pos
    )
    :   pos_{pos}
    {

    }

    C_CameraPosition2DIso(
        float x,
        float y
    )
    :   C_CameraPosition2DIso{vec2(x, y)}
    {

    }

    // Default
    C_CameraPosition2DIso()
    :   C_CameraPosition2DIso{vec2(0, 0)}
    {

    }

};

} // namespace IRComponents

#endif /* COMPONENT_CAMERA_POSITION_2D_ISO_H */

/*
 * Project: Irreden Engine
 * File: component_position_2d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: December 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_POSITION_2D_ISO_H
#define COMPONENT_POSITION_2D_ISO_H

#include <irreden/ir_math.hpp>

using IRMath::vec2;

namespace IRComponents {

    struct C_Position2DIso {
        vec2 pos_;

        C_Position2DIso(vec2 pos)
        :   pos_{pos}
        {

        }

        C_Position2DIso(float x, float y)
        :   C_Position2DIso{vec2(x, y)}
        {

        }

        C_Position2DIso()
        :   C_Position2DIso{vec2(0, 0)}
        {

        }

    };

} // namespace IRComponents

#endif /* COMPONENT_POSITION_2D_ISO_H */

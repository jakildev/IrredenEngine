/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_position_2d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_POSITION_2D_H
#define COMPONENT_POSITION_2D_H

#include "../math/ir_math.hpp"

using IRMath::vec2;

namespace IRComponents {

    struct C_Position2D {
        vec2 pos_;

        C_Position2D(vec2 pos)
        :   pos_{pos}
        {

        }

        C_Position2D(float x, float y)
        :   C_Position2D{vec2(x, y)}
        {

        }

        // Default
        C_Position2D()
        :   C_Position2D{vec2(0, 0)}
        {

        }


    };

} // namespace IRComponents

#endif /* COMPONENT_POSITION_2D_H */

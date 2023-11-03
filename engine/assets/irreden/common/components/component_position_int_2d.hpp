/*
 * Project: Irreden Engine
 * File: component_position_int_2d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_POSITION_INT_2D_H
#define COMPONENT_POSITION_INT_2D_H

#include <irreden/ir_math.hpp>

using IRMath::ivec2;

namespace IRComponents {

    struct C_PositionInt2D {
        ivec2 pos_;

        C_PositionInt2D(ivec2 pos)
        :   pos_{pos}
        {

        }

        C_PositionInt2D(int x, int y)
        :   C_PositionInt2D{ivec2(x, y)}
        {

        }

        C_PositionInt2D()
        :   C_PositionInt2D{ivec2(0, 0)}
        {

        }


    };

} // namespace IRComponents

#endif /* COMPONENT_POSITION_INT_2D_H */

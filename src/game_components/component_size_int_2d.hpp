/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_size_int_2d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_SIZE_INT_2D_H
#define COMPONENT_SIZE_INT_2D_H

#include "../math/ir_math.hpp"
#include "../world/ir_constants.hpp"
#include "component_tags_all.hpp"

using namespace IRMath;

namespace IRComponents {

    // Used for 2d game logic, usually representing 2 dimentions
    // of a 3d voxel set...
    struct C_SizeInt2D {
        ivec2 size_;

        C_SizeInt2D(ivec2 size)
        :   size_{size}
        {

        }

        C_SizeInt2D(int x, int y)
        :   C_SizeInt2D{ivec2(x, y)}
        {

        }

        // Default
        C_SizeInt2D()
        :   C_SizeInt2D{ivec2(0, 0)}
        {

        }

    };

} // namespace IRComponents




#endif /* COMPONENT_SIZE_INT_2D_H */

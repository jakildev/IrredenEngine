/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_size_int_3d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_SIZE_INT_3D_H
#define COMPONENT_SIZE_INT_3D_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;

namespace IRComponents {

    struct C_SizeInt3D {
        ivec3 size_;

        C_SizeInt3D(ivec3 size)
        :   size_{size}
        {

        }

        C_SizeInt3D(int x, int y, int z)
        :   C_SizeInt3D{ivec3(x, y, z)}
        {

        }

        // Default
        C_SizeInt3D()
        :   C_SizeInt3D{ivec3(0, 0, 0)}
        {

        }

    };

} // namespace IRComponents




#endif /* COMPONENT_SIZE_INT_3D_H */

/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_size_triangles.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_SIZE_TRIANGLES_H
#define COMPONENT_SIZE_TRIANGLES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;

namespace IRComponents {

    // Used for the size of the triangle data, usually
    // representing a 3d voxel set
    struct C_SizeTriangles {
        ivec2 size_;

        C_SizeTriangles(ivec2 size)
        :   size_{size}
        {

        }

        C_SizeTriangles(int x, int y)
        :   C_SizeTriangles{ivec2(x, y)}
        {

        }

        // Default
        C_SizeTriangles()
        :   C_SizeTriangles{ivec2(0, 0)}
        {

        }

    };

} // namespace IRComponents


#endif /* COMPONENT_SIZE_TRIANGLES_H */

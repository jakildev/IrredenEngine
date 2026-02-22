#ifndef COMPONENT_SIZE_TRIANGLES_H
#define COMPONENT_SIZE_TRIANGLES_H

#include <irreden/ir_math.hpp>

using IRMath::ivec2;

namespace IRComponents {

// Used for the size of the triangle data, usually
// representing a 3d voxel set
struct C_SizeTriangles {
    ivec2 size_;

    C_SizeTriangles(ivec2 size)
        : size_{size} {}

    C_SizeTriangles(int x, int y)
        : C_SizeTriangles{ivec2(x, y)} {}

    C_SizeTriangles()
        : C_SizeTriangles{ivec2(0, 0)} {}
};

} // namespace IRComponents

#endif /* COMPONENT_SIZE_TRIANGLES_H */
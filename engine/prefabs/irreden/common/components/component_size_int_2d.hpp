#ifndef COMPONENT_SIZE_INT_2D_H
#define COMPONENT_SIZE_INT_2D_H

#include <irreden/ir_math.hpp>

using IRMath::ivec2;

namespace IRComponents {

// Used for 2d game logic, usually representing 2 dimentions
// of a 3d voxel set...
struct C_SizeInt2D {
    ivec2 size_;

    C_SizeInt2D(ivec2 size)
        : size_{size} {}

    C_SizeInt2D(int x, int y)
        : C_SizeInt2D{ivec2(x, y)} {}

    C_SizeInt2D()
        : C_SizeInt2D{ivec2(0, 0)} {}
};

} // namespace IRComponents

#endif /* COMPONENT_SIZE_INT_2D_H */

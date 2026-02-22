#ifndef COMPONENT_SIZE_INT_3D_H
#define COMPONENT_SIZE_INT_3D_H

#include <irreden/ir_math.hpp>

using IRMath::ivec3;

namespace IRComponents {

struct C_SizeInt3D {
    ivec3 size_;

    C_SizeInt3D(ivec3 size)
        : size_{size} {}

    C_SizeInt3D(int x, int y, int z)
        : C_SizeInt3D{ivec3(x, y, z)} {}

    C_SizeInt3D()
        : C_SizeInt3D{ivec3(0, 0, 0)} {}
};

} // namespace IRComponents

#endif /* COMPONENT_SIZE_INT_3D_H */

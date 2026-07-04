#ifndef COMPONENT_POSITION_INT_3D_H
#define COMPONENT_POSITION_INT_3D_H

#include <irreden/ir_math.hpp>

using IRMath::ivec3;

namespace IRComponents {

struct C_PositionInt3D {
    ivec3 pos_;

    C_PositionInt3D(ivec3 pos)
        : pos_{pos} {}

    C_PositionInt3D(int x, int y, int z)
        : C_PositionInt3D{ivec3(x, y, z)} {}

    C_PositionInt3D()
        : C_PositionInt3D{ivec3(0, 0, 0)} {}
};

} // namespace IRComponents

#endif /* COMPONENT_POSITION_INT_3D_H */

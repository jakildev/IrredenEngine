#ifndef COMPONENT_POSITION_GLOBAL_3D_H
#define COMPONENT_POSITION_GLOBAL_3D_H

#include <irreden/ir_math.hpp>

using IRMath::vec3;

namespace IRComponents {

struct C_PositionGlobal3D {
    vec3 pos_;
    float tempPackBuffer_;

    C_PositionGlobal3D(vec3 pos) : pos_{pos} {}

    C_PositionGlobal3D(float x, float y, float z) : C_PositionGlobal3D{vec3(x, y, z)} {}

    C_PositionGlobal3D() : C_PositionGlobal3D{vec3(0, 0, 0)} {}
};

} // namespace IRComponents

#endif /* COMPONENT_POSITION_GLOBAL_3D_H */

#ifndef COMPONENT_ACCELERATION_3D_H
#define COMPONENT_ACCELERATION_3D_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

using IRMath::vec3;

namespace IRComponents {

// Acceleration is in blocks per second per second
struct C_Acceleration3D {
    vec3 acceleration_;

    C_Acceleration3D(vec3 acceleration)
        : acceleration_(acceleration) {}

    C_Acceleration3D(float x, float y, float z)
        : C_Acceleration3D(vec3{x, y, z}) {}

    C_Acceleration3D()
        : acceleration_(vec3(0.0f)) {}
};

} // namespace IRComponents

#endif /* COMPONENT_ACCELERATION_3D_H */

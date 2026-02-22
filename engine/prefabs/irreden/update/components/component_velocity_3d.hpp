#ifndef COMPONENT_VELOCITY_3D_H
#define COMPONENT_VELOCITY_3D_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

using namespace IRMath;

namespace IRComponents {

// Velocity is in blocks per second
struct C_Velocity3D {
    vec3 velocity_;

    // Velocity is defined in blocks per second
    // TODO: Use the fixed update delta time instead of converting
    // to frames per second here!
    C_Velocity3D(vec3 velocity)
        : velocity_(velocity) {}

    C_Velocity3D(float x, float y, float z)
        : C_Velocity3D(vec3{x, y, z}) {}

    // Default
    C_Velocity3D()
        : C_Velocity3D(vec3(0.0f)) {}

    void tick() {}
};

} // namespace IRComponents

#endif /* COMPONENT_VELOCITY_3D_H */

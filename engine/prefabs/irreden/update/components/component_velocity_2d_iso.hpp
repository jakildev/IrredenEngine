#ifndef COMPONENT_VELOCITY_2D_ISO_H
#define COMPONENT_VELOCITY_2D_ISO_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

using namespace IRMath;

namespace IRComponents {

// Velocity is in blocks per second
struct C_Velocity2DIso {
    vec2 velocity_;

    // Velocity is defined in trixels per second
    C_Velocity2DIso(vec2 velocity)
        : velocity_{velocity} {}

    C_Velocity2DIso(float x, float y)
        : C_Velocity2DIso(vec2{x, y}) {}

    // Default
    C_Velocity2DIso()
        : C_Velocity2DIso(vec2(0.0f)) {}
};

} // namespace IRComponents

#endif /* COMPONENT_VELOCITY_2D_ISO_H */
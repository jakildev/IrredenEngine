#ifndef IR_MATH_PHYSICS_H
#define IR_MATH_PHYSICS_H

#include <cmath>

namespace IRMath {

// Upward impulse speed for a symmetric ballistic arc reaching height h
// under constant gravity g.
inline float impulseForHeight(float gravity, float height) {
    return std::sqrt(2.0f * gravity * height);
}

// Total flight time (up + down) for a symmetric arc under gravity g
// reaching height h.
inline float flightTimeForHeight(float gravity, float height) {
    return 2.0f * std::sqrt(2.0f * height / gravity);
}

// Height reached by a given impulse speed under gravity g.
constexpr float heightForImpulse(float gravity, float impulseSpeed) {
    return (impulseSpeed * impulseSpeed) / (2.0f * gravity);
}

// Max position displacement in a single frame at a given velocity.
constexpr float maxFrameDisplacement(float velocity, float dt) {
    return velocity * dt;
}

// Whether the combined collider thickness is sufficient to prevent
// tunneling at the given max velocity and timestep.
constexpr bool isTunnelingSafe(
    float maxVelocity,
    float dt,
    float colliderThicknessA,
    float colliderThicknessB
) {
    return maxFrameDisplacement(maxVelocity, dt) < (colliderThicknessA + colliderThicknessB);
}

} // namespace IRMath

#endif /* IR_MATH_PHYSICS_H */

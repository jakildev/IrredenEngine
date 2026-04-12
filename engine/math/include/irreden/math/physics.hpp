#ifndef IR_MATH_PHYSICS_H
#define IR_MATH_PHYSICS_H

#include <cmath>

namespace IRMath {

/// Upward impulse speed needed to reach `height` under constant `gravity`.
/// Derived from energy conservation: v = √(2·g·h).
/// Both `gravity` and `height` must be non-negative; negative inputs produce NaN.
inline float impulseForHeight(float gravity, float height) {
    return std::sqrt(2.0f * gravity * height);
}

/// Total flight time (up + down) for a symmetric arc reaching `height` under
/// constant `gravity`. Derived from t = 2·√(2h/g).
/// The arc is symmetric: time to peak equals time back to the launch height.
inline float flightTimeForHeight(float gravity, float height) {
    return 2.0f * std::sqrt(2.0f * height / gravity);
}

/// Maximum height reached by a projectile launched at `impulseSpeed` under
/// constant `gravity`. Derived from h = v² / (2g).
constexpr float heightForImpulse(float gravity, float impulseSpeed) {
    return (impulseSpeed * impulseSpeed) / (2.0f * gravity);
}

/// Maximum position displacement in one frame at `velocity` with timestep `dt`.
/// Used as the tunneling threshold: if an object can move more than the
/// combined collider thickness in one frame it may skip through geometry.
constexpr float maxFrameDisplacement(float velocity, float dt) {
    return velocity * dt;
}

/// Returns true if the combined collider thickness is large enough to prevent
/// tunneling at the given max velocity and timestep.
/// Safe when: maxFrameDisplacement(maxVelocity, dt) < colliderA + colliderB.
/// If this returns false, reduce `dt`, reduce `maxVelocity`, or thicken the
/// colliders to restore safety.
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

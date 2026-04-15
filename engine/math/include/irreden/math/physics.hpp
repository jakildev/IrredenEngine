#ifndef IR_MATH_PHYSICS_H
#define IR_MATH_PHYSICS_H

#include <cmath>

namespace IRMath {

/// Upward impulse speed (in units/s) required for a symmetric ballistic arc
/// reaching @p height under constant gravity @p gravity.
/// Formula: `sqrt(2 * g * h)`.
inline float impulseForHeight(float gravity, float height) {
    return std::sqrt(2.0f * gravity * height);
}

/// Total flight time (up + down) for a symmetric arc under constant gravity
/// @p gravity reaching @p height.
/// Formula: `2 * sqrt(2h / g)`.
inline float flightTimeForHeight(float gravity, float height) {
    return 2.0f * std::sqrt(2.0f * height / gravity);
}

/// Peak height reached by an object launched at @p impulseSpeed under
/// constant gravity @p gravity.
/// Formula: `v² / (2g)`.
constexpr float heightForImpulse(float gravity, float impulseSpeed) {
    return (impulseSpeed * impulseSpeed) / (2.0f * gravity);
}

/// Maximum position displacement in one frame: `velocity * dt`.
/// Used as the reference distance for tunneling checks.
constexpr float maxFrameDisplacement(float velocity, float dt) {
    return velocity * dt;
}

/// Returns `true` when the combined thickness of two colliders is large
/// enough that a body moving at @p maxVelocity cannot pass through both in
/// a single timestep @p dt.
/// Check: `maxVelocity * dt < colliderThicknessA + colliderThicknessB`.
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

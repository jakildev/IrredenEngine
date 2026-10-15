#ifndef IR_MATH_PHYSICS_H
#define IR_MATH_PHYSICS_H

#include <cmath>

#include <irreden/math/ir_math_types.hpp>

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

/// Whether box A, which ends a step at [@p minA, @p maxA] after moving by
/// @p motion relative to box B, touched B at any moment of the step. Per axis,
/// the slab entry/exit fractions of the step; the intervals of the three axes
/// must share a moment in [0, 1]. Boundaries are inclusive, so a zero
/// @p motion reduces to the static closed-interval overlap test.
inline bool sweptAabbOverlap(vec3 minA, vec3 maxA, vec3 minB, vec3 maxB, vec3 motion) {
    float enter = 0.0f;
    float exit = 1.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const float startMin = minA[axis] - motion[axis];
        const float startMax = maxA[axis] - motion[axis];
        const float step = motion[axis];
        if (step == 0.0f) {
            if (startMin > maxB[axis] || startMax < minB[axis]) {
                return false;
            }
            continue;
        }
        float tFirst = (minB[axis] - startMax) / step;
        float tLast = (maxB[axis] - startMin) / step;
        if (tFirst > tLast) {
            const float swap = tFirst;
            tFirst = tLast;
            tLast = swap;
        }
        enter = tFirst > enter ? tFirst : enter;
        exit = tLast < exit ? tLast : exit;
        if (enter > exit) {
            return false;
        }
    }
    return true;
}

} // namespace IRMath

#endif /* IR_MATH_PHYSICS_H */

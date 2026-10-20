#ifndef COMPONENT_COLLISION_RESPONSE_H
#define COMPONENT_COLLISION_RESPONSE_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

// Rest needs `gravity * deltaTime(UPDATE) < restSpeed_`: at kFPS 60 the
// default covers gravity below 120 blocks/s^2.
constexpr float kCollisionRestSpeedDefault = 2.0f;

// Marks a moving body for COLLISION_RESPONSE: on contact with a solid
// collider or the floor plane, the normal velocity reflects scaled by
// `restitution_` and the tangential velocity scales by (1 - `friction_`).
// `floorZ_` is an infinite plane whose solid side is +z (up is -z), so the
// body's box bottom never ends a tick below it.
struct C_CollisionResponse {
    float restitution_;
    float friction_;
    // Blocks/s. An upward-facing contact approached slower than this rests
    // instead of reflecting, and zeroes a tangential speed under it.
    float restSpeed_;
    float floorZ_;
    bool hasFloor_;
    bool restOnSupport_;
    // Set by COLLISION_RESPONSE on a tick that rested the body on a support;
    // cleared at the start of each of its ticks.
    bool resting_;

    C_CollisionResponse(float restitution, float friction, bool restOnSupport)
        : restitution_{IRMath::clamp(restitution, 0.0f, 1.0f)}
        , friction_{IRMath::clamp(friction, 0.0f, 1.0f)}
        , restSpeed_{kCollisionRestSpeedDefault}
        , floorZ_{0.0f}
        , hasFloor_{false}
        , restOnSupport_{restOnSupport}
        , resting_{false} {}

    C_CollisionResponse(float restitution, float friction, bool restOnSupport, float floorZ)
        : C_CollisionResponse(restitution, friction, restOnSupport) {
        floorZ_ = floorZ;
        hasFloor_ = true;
    }

    C_CollisionResponse()
        : C_CollisionResponse(0.0f, 0.0f, true) {}
};

} // namespace IRComponents

#endif /* COMPONENT_COLLISION_RESPONSE_H */

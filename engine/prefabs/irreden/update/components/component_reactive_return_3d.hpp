#ifndef COMPONENT_REACTIVE_RETURN_3D_H
#define COMPONENT_REACTIVE_RETURN_3D_H

#include <irreden/ir_math.hpp>

using IRMath::vec3;

namespace IRComponents {

// Generic response behavior:
// - apply a configurable velocity impulse on trigger
// - return to origin with damped spring motion
// - settle after configurable rebound count
struct C_ReactiveReturn3D {
    vec3 origin_;
    bool originInitialized_;

    vec3 triggerImpulseVelocity_;
    bool triggerOnContactEnter_;

    float springStrength_;
    float dampingPerSecond_;
    int maxRebounds_;
    int reboundCount_;
    vec3 previousError_;

    float settleDistance_;
    float settleSpeed_;
    bool active_;

    C_ReactiveReturn3D(vec3 triggerImpulseVelocity, float springStrength, float dampingPerSecond,
                       int maxRebounds, float settleDistance, float settleSpeed,
                       bool triggerOnContactEnter = true)
        : origin_{vec3(0.0f)}, originInitialized_{false},
          triggerImpulseVelocity_{triggerImpulseVelocity},
          triggerOnContactEnter_{triggerOnContactEnter}, springStrength_{springStrength},
          dampingPerSecond_{dampingPerSecond}, maxRebounds_{maxRebounds}, reboundCount_{0},
          previousError_{vec3(0.0f)}, settleDistance_{settleDistance}, settleSpeed_{settleSpeed},
          active_{false} {}

    C_ReactiveReturn3D()
        : C_ReactiveReturn3D(vec3(0.0f, 0.0f, 18.0f), 55.0f, 10.0f, 2, 0.03f, 0.05f, true) {}
};

} // namespace IRComponents

#endif /* COMPONENT_REACTIVE_RETURN_3D_H */

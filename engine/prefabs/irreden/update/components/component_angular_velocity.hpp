#ifndef COMPONENT_ANGULAR_VELOCITY_H
#define COMPONENT_ANGULAR_VELOCITY_H

// C_AngularVelocity — a spin that decays to rest. Pair with C_LocalTransform;
// ANGULAR_VELOCITY_DAMPED rotates the entity about `axis_` by
// `radiansPerFrame_` each UPDATE tick, then scales the rate by
// `(1 - dampingPerFrame_)`. `impulse()` is how the spin starts and how it gets
// nudged again while it is still turning.
//
// Distinct from C_AutoSpin, whose rate is constant: an entity may carry both,
// and the impulse then decays on top of the steady spin (AUTO_SPIN_LOCAL_TRANSFORM
// first, ANGULAR_VELOCITY_DAMPED second, both before PROPAGATE_TRANSFORM).
//
// Time to rest: after n ticks the rate is `r0 * (1 - dampingPerFrame_)^n`, and
// the system snaps it to exactly 0 once it falls below `kAngularRestEpsilon`,
// so an impulse of `r0` is at rest within
// `ceil(ln(kAngularRestEpsilon / r0) / ln(1 - dampingPerFrame_))` ticks —
// `ticksToRest()` returns the exact count, or -1 when the spin never decays.
// `dampingPerFrame_` is clamped to [0, 1] by the system: 0 never decays, 1 stops
// after a single tick.
//
// Rates are per UPDATE tick, matching C_AutoSpin.

#include <irreden/ir_math.hpp>

namespace IRComponents {

struct C_AngularVelocity {
    static constexpr float kAngularRestEpsilon = 1.0e-4f;
    static constexpr float kDefaultDampingPerFrame = 0.05f;

    // Need not be unit; a zero axis is a no-op.
    IRMath::vec3 axis_ = IRMath::vec3(0.0f, 0.0f, 1.0f);
    float radiansPerFrame_ = 0.0f;
    float dampingPerFrame_ = kDefaultDampingPerFrame;

    C_AngularVelocity() = default;

    C_AngularVelocity(
        IRMath::vec3 axis, float radiansPerFrame, float dampingPerFrame = kDefaultDampingPerFrame
    )
        : axis_{axis}
        , radiansPerFrame_{radiansPerFrame}
        , dampingPerFrame_{dampingPerFrame} {}

    // Ticks ANGULAR_VELOCITY_DAMPED takes to bring a spin of `radiansPerFrame`
    // to rest from standstill. `dampingPerFrame` is clamped to [0, 1] as the
    // system does; -1 when the rate would never fall (damping 0, NaN, or too
    // small to move a float) and the spin is above rest.
    static constexpr int ticksToRest(float radiansPerFrame, float dampingPerFrame) {
        const float damping =
            dampingPerFrame < 0.0f ? 0.0f : (dampingPerFrame > 1.0f ? 1.0f : dampingPerFrame);
        const float decay = 1.0f - damping;
        float rate = radiansPerFrame < 0.0f ? -radiansPerFrame : radiansPerFrame;
        if (!(decay < 1.0f)) {
            return rate >= kAngularRestEpsilon ? -1 : 0;
        }
        int ticks = 0;
        for (; rate >= kAngularRestEpsilon; rate *= decay) {
            ++ticks;
        }
        return ticks;
    }

    // Adds `radiansPerFrame` about `axis` to the current spin as angular
    // velocity vectors: the same axis sums the rates, a different axis yields
    // the combined axis and rate. Leaves `axis_` unit and the rate
    // non-negative. A zero `axis` or rate is a no-op.
    void impulse(IRMath::vec3 axis, float radiansPerFrame) {
        const float axisLength = IRMath::length(axis);
        if (axisLength == 0.0f || radiansPerFrame == 0.0f) {
            return;
        }
        IRMath::vec3 angular = axis * (radiansPerFrame / axisLength);
        const float ownLength = IRMath::length(axis_);
        if (ownLength != 0.0f) {
            angular += axis_ * (radiansPerFrame_ / ownLength);
        }
        const float rate = IRMath::length(angular);
        if (rate == 0.0f) {
            // Equal and opposite: at rest, keep the last axis.
            radiansPerFrame_ = 0.0f;
            return;
        }
        axis_ = angular / rate;
        radiansPerFrame_ = rate;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_ANGULAR_VELOCITY_H */

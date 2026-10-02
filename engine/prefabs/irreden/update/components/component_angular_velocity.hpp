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
// The system clamps `dampingPerFrame_` to [0, 1] (`effectiveDamping()`): 0 or
// NaN never decays, 1 stops after a single tick. A rate under
// `kAngularRestEpsilon` or non-finite is already at rest (`effectiveRate()`):
// the system zeroes it without turning the entity.
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

    // The damping ANGULAR_VELOCITY_DAMPED applies: clamped to [0, 1], with NaN
    // read as 0 (no decay) so it can never reach the rate.
    static constexpr float effectiveDamping(float dampingPerFrame) {
        if (!(dampingPerFrame > 0.0f)) {
            return 0.0f;
        }
        return dampingPerFrame < 1.0f ? dampingPerFrame : 1.0f;
    }

    // The rate ANGULAR_VELOCITY_DAMPED turns by: `radiansPerFrame` itself, or
    // 0 when the spin is at rest — under `kAngularRestEpsilon` in magnitude,
    // or non-finite (±inf, NaN), which no rotation can represent.
    static constexpr float effectiveRate(float radiansPerFrame) {
        const float magnitude = IRMath::abs(radiansPerFrame);
        if (!(magnitude >= kAngularRestEpsilon) || !IRMath::isFinite(magnitude)) {
            return 0.0f;
        }
        return radiansPerFrame;
    }

    // Ticks ANGULAR_VELOCITY_DAMPED turns a spin of `radiansPerFrame` before it
    // is at rest, under `effectiveRate()` and `effectiveDamping()`; -1 when the
    // rate would never fall (damping 0, NaN, or too small to move a float).
    // The loop ends for every input: the rate is finite, and a decay under 1
    // lowers it on every multiply.
    static constexpr int ticksToRest(float radiansPerFrame, float dampingPerFrame) {
        float rate = IRMath::abs(effectiveRate(radiansPerFrame));
        if (rate == 0.0f) {
            return 0;
        }
        const float decay = 1.0f - effectiveDamping(dampingPerFrame);
        if (!(decay < 1.0f)) {
            return -1;
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
    // non-negative. A zero or non-finite `axis` or rate is a no-op, and so is
    // an impulse whose sum overflows; a current spin that is non-finite
    // contributes nothing.
    void impulse(IRMath::vec3 axis, float radiansPerFrame) {
        const float axisLength = IRMath::length(axis);
        if (axisLength == 0.0f || radiansPerFrame == 0.0f || !IRMath::isFinite(axisLength) ||
            !IRMath::isFinite(radiansPerFrame)) {
            return;
        }
        IRMath::vec3 angular = axis * (radiansPerFrame / axisLength);
        const float ownLength = IRMath::length(axis_);
        if (ownLength != 0.0f && IRMath::isFinite(ownLength) &&
            IRMath::isFinite(radiansPerFrame_)) {
            angular += axis_ * (radiansPerFrame_ / ownLength);
        }
        const float rate = IRMath::length(angular);
        if (rate == 0.0f) {
            // Equal and opposite: at rest, keep the last axis.
            radiansPerFrame_ = 0.0f;
            return;
        }
        if (!IRMath::isFinite(rate)) {
            return;
        }
        axis_ = angular / rate;
        radiansPerFrame_ = rate;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_ANGULAR_VELOCITY_H */

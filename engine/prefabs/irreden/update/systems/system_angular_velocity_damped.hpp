#ifndef SYSTEM_ANGULAR_VELOCITY_DAMPED_H
#define SYSTEM_ANGULAR_VELOCITY_DAMPED_H

// SYSTEM_ANGULAR_VELOCITY_DAMPED — rotates each entity about
// `C_AngularVelocity::axis_` by its current rate, then decays the rate by
// `dampingPerFrame_` and snaps it to 0 under `kAngularRestEpsilon`. Composed
// on the LEFT of the local rotation, the same convention as
// AUTO_SPIN_LOCAL_TRANSFORM, so the two stack on one entity.
//
// Register in UPDATE after AUTO_SPIN_LOCAL_TRANSFORM (when both are present)
// and before PROPAGATE_TRANSFORM.
//
// Reads and writes only the iterating entity's own C_LocalTransform and
// C_AngularVelocity, so PARALLEL_FOR is safe. Impulses arrive from outside
// this tick (another system, a command handler).

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/update/components/component_angular_velocity.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<ANGULAR_VELOCITY_DAMPED> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    void tick(C_LocalTransform &localXform, C_AngularVelocity &spin) {
        // A zero axis with a non-zero rate would hand `quatAxisAngle` a NaN
        // normalize; the rate still decays so the component comes to rest.
        if (spin.radiansPerFrame_ == 0.0f) {
            return;
        }
        if (IRMath::dot(spin.axis_, spin.axis_) != 0.0f) {
            const vec4 delta = IRMath::quatAxisAngle(spin.axis_, spin.radiansPerFrame_);
            localXform.rotation_ = IRMath::quatMul(delta, localXform.rotation_);
        }
        spin.radiansPerFrame_ *= 1.0f - C_AngularVelocity::effectiveDamping(spin.dampingPerFrame_);
        if (IRMath::abs(spin.radiansPerFrame_) < C_AngularVelocity::kAngularRestEpsilon) {
            spin.radiansPerFrame_ = 0.0f;
        }
    }

    static SystemId create() {
        return registerSystem<ANGULAR_VELOCITY_DAMPED, C_LocalTransform, C_AngularVelocity>(
            "AngularVelocityDamped"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ANGULAR_VELOCITY_DAMPED_H */

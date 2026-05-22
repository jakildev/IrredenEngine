#ifndef SYSTEM_REACTIVE_RETURN_3D_H
#define SYSTEM_REACTIVE_RETURN_3D_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_reactive_return_3d.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<REACTIVE_RETURN_3D> {
    static SystemId create() {
        return createSystem<C_LocalTransform, C_Velocity3D, C_ContactEvent, C_ReactiveReturn3D>(
            "ReactiveReturn3D",
            [](C_LocalTransform &localXform,
               C_Velocity3D &velocity,
               const C_ContactEvent &contact,
               C_ReactiveReturn3D &reactive) {
                float dt = IRTime::deltaTime(IRTime::UPDATE);

                if (!reactive.originInitialized_) {
                    reactive.origin_ = localXform.translation_;
                    reactive.previousError_ = reactive.origin_ - localXform.translation_;
                    reactive.originInitialized_ = true;
                }

                if (reactive.triggerOnContactEnter_ && contact.entered_) {
                    velocity.velocity_ += reactive.triggerImpulseVelocity_;
                    reactive.active_ = true;
                }

                if (!reactive.active_) {
                    return;
                }

                vec3 error = reactive.origin_ - localXform.translation_;
                float errorLen = IRMath::length(error);
                float speedLen = IRMath::length(velocity.velocity_);

                if (IRMath::dot(error, reactive.previousError_) < 0.0f) {
                    reactive.reboundCount_++;
                }
                reactive.previousError_ = error;

                if (reactive.reboundCount_ >= reactive.maxRebounds_ &&
                    errorLen <= reactive.settleDistance_ && speedLen <= reactive.settleSpeed_) {
                    localXform.translation_ = reactive.origin_;
                    velocity.velocity_ = vec3(0.0f);
                    reactive.active_ = false;
                    reactive.reboundCount_ = 0;
                    reactive.previousError_ = vec3(0.0f);
                    return;
                }

                velocity.velocity_ += error * (reactive.springStrength_ * dt);
                float damp = IRMath::max(0.0f, 1.0f - reactive.dampingPerSecond_ * dt);
                velocity.velocity_ *= damp;
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_REACTIVE_RETURN_3D_H */

#ifndef SYSTEM_VELOCITY_DRAG_H
#define SYSTEM_VELOCITY_DRAG_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_velocity_drag.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<VELOCITY_DRAG> {
    static SystemId create() {
        return createSystem<C_Velocity3D, C_VelocityDrag>(
            "VelocityDrag",
            [](C_Velocity3D &velocity, C_VelocityDrag &drag) {
                float deltaSeconds = IRTime::deltaTime(IRTime::UPDATE);
                drag.elapsedSeconds_ += deltaSeconds;

                float dampFactor = IRMath::max(0.0f, 1.0f - (drag.dragPerSecond_ * deltaSeconds));
                velocity.velocity_ *= dampFactor;

                if (IRMath::length(velocity.velocity_) < drag.minSpeed_) {
                    velocity.velocity_ = vec3(0.0f);
                }

                if (drag.elapsedSeconds_ >= drag.driftDelaySeconds_) {
                    velocity.velocity_.z -= drag.driftUpAccelPerSecond_ * deltaSeconds;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VELOCITY_DRAG_H */

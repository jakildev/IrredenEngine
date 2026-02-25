#ifndef SYSTEM_VELOCITY_DRAG_H
#define SYSTEM_VELOCITY_DRAG_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_velocity_drag.hpp>

#include <cmath>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<VELOCITY_DRAG> {
    static SystemId create() {
        return createSystem<C_Velocity3D, C_VelocityDrag>(
            "VelocityDrag",
            [](C_Velocity3D &velocity, C_VelocityDrag &drag) {
                float dt = IRTime::deltaTime(IRTime::UPDATE);
                drag.elapsedSeconds_ += dt;

                // XY drag is always active.
                vec3 axisScale = IRMath::max(
                    vec3(drag.dragScaleX_, drag.dragScaleY_, drag.dragScaleZ_),
                    vec3(0.0f)
                );

                bool inBlendOrHover =
                    !drag.zSettled_ &&
                    drag.elapsedSeconds_ >= drag.driftDelaySeconds_ &&
                    drag.hoverDurationSeconds_ > 0.0f;

                if (inBlendOrHover) {
                    drag.hoverElapsedSec_ += dt;
                    float blendDur = std::max(drag.hoverBlendSeconds_, 0.001f);
                    float rawT = IRMath::clamp(
                        drag.hoverElapsedSec_ / blendDur, 0.0f, 1.0f);
                    float blend = kEasingFunctions.at(drag.hoverBlendEasing_)(rawT);

                    float hoverVel =
                        std::sin(drag.hoverElapsedSec_ * drag.hoverOscSpeed_)
                        * drag.hoverOscAmplitude_ * blend;

                    // Fade out Z drag as hover fades in
                    axisScale.z *= (1.0f - blend);

                    vec3 dampFactor = IRMath::max(
                        vec3(0.0f),
                        vec3(1.0f) - (axisScale * (drag.dragPerSecond_ * dt))
                    );
                    velocity.velocity_ *= dampFactor;

                    velocity.velocity_.z =
                        velocity.velocity_.z * (1.0f - blend) + hoverVel;

                    if (rawT >= 1.0f) {
                        drag.zSettled_ = true;
                    }
                } else if (drag.zSettled_) {
                    // Z drag disabled, hover/exit drives Z.
                    axisScale.z = 0.0f;
                    vec3 dampFactor = IRMath::max(
                        vec3(0.0f),
                        vec3(1.0f) - (axisScale * (drag.dragPerSecond_ * dt))
                    );
                    velocity.velocity_ *= dampFactor;

                    if (drag.hoverDurationSeconds_ > 0.0f
                        && drag.hoverElapsedSec_ < drag.hoverDurationSeconds_) {
                        drag.hoverElapsedSec_ += dt;
                        float hoverT = drag.hoverElapsedSec_;
                        velocity.velocity_.z =
                            std::sin(hoverT * drag.hoverOscSpeed_)
                            * drag.hoverOscAmplitude_;
                    } else {
                        velocity.velocity_.z -= drag.driftUpAccelPerSecond_ * dt;
                    }
                } else {
                    // Pre-blend: pure drag on all axes.
                    vec3 dampFactor = IRMath::max(
                        vec3(0.0f),
                        vec3(1.0f) - (axisScale * (drag.dragPerSecond_ * dt))
                    );
                    velocity.velocity_ *= dampFactor;
                }

                if (std::abs(velocity.velocity_.x) < drag.minSpeed_)
                    velocity.velocity_.x = 0.0f;
                if (std::abs(velocity.velocity_.y) < drag.minSpeed_)
                    velocity.velocity_.y = 0.0f;
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VELOCITY_DRAG_H */

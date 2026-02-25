#ifndef COMPONENT_VELOCITY_DRAG_H
#define COMPONENT_VELOCITY_DRAG_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

struct C_VelocityDrag {
    float dragPerSecond_;
    float driftDelaySeconds_;
    float driftUpAccelPerSecond_;
    float minSpeed_;
    float dragScaleX_;
    float dragScaleY_;
    float dragScaleZ_;
    float hoverDurationSeconds_;
    float hoverOscSpeed_;
    float hoverOscAmplitude_;
    float hoverBlendSeconds_;
    IRMath::IREasingFunctions hoverBlendEasing_;
    float elapsedSeconds_;
    float hoverElapsedSec_ = 0.0f;
    bool  zSettled_         = false;

    C_VelocityDrag(
        float dragPerSecond,
        float driftDelaySeconds,
        float driftUpAccelPerSecond,
        float minSpeed = 0.01f,
        float dragScaleX = 1.0f,
        float dragScaleY = 1.0f,
        float dragScaleZ = 1.0f,
        float hoverDurationSeconds = 0.0f,
        float hoverOscSpeed = 0.0f,
        float hoverOscAmplitude = 0.0f,
        float hoverBlendSeconds = 0.5f,
        IRMath::IREasingFunctions hoverBlendEasing = IRMath::IREasingFunctions::kLinearInterpolation
    )
        : dragPerSecond_{dragPerSecond}
        , driftDelaySeconds_{driftDelaySeconds}
        , driftUpAccelPerSecond_{driftUpAccelPerSecond}
        , minSpeed_{minSpeed}
        , dragScaleX_{dragScaleX}
        , dragScaleY_{dragScaleY}
        , dragScaleZ_{dragScaleZ}
        , hoverDurationSeconds_{hoverDurationSeconds}
        , hoverOscSpeed_{hoverOscSpeed}
        , hoverOscAmplitude_{hoverOscAmplitude}
        , hoverBlendSeconds_{hoverBlendSeconds}
        , hoverBlendEasing_{hoverBlendEasing}
        , elapsedSeconds_{0.0f} {}

    C_VelocityDrag()
        : C_VelocityDrag(1.8f, 0.12f, 5.0f, 0.01f, 1.0f, 1.0f, 1.0f) {}
};

} // namespace IRComponents

#endif /* COMPONENT_VELOCITY_DRAG_H */

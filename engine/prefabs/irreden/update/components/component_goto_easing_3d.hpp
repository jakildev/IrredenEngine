#ifndef COMPONENT_GOTO_EASING_3D_H
#define COMPONENT_GOTO_EASING_3D_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_GotoEasing3D {
    IRMath::vec3 startPos_;
    IRMath::vec3 endPos_;
    int durationFrames_;
    int currentFrame_;
    GLMEasingFunction easingFunction_;
    bool done_ = false;

    C_GotoEasing3D(
        IRMath::vec3 start,
        IRMath::vec3 end,
        float durationSeconds,
        IREasingFunctions easingFunction = IREasingFunctions::kLinearInterpolation
    )
        : startPos_{start}
        , endPos_{end}
        , durationFrames_{IRMath::secondsToFrames<IRConstants::kFPS>(durationSeconds)}
        , currentFrame_{0}
        , easingFunction_{kEasingFunctions.at(easingFunction)} {}

    C_GotoEasing3D()
        : C_GotoEasing3D{IRMath::vec3{0.0f}, IRMath::vec3{0.0f}, 0.0f} {}
};

} // namespace IRComponents

#endif /* COMPONENT_GOTO_EASING_3D_H */

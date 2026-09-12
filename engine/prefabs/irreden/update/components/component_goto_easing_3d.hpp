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
    // Authored curve identity, resolved per tick by GOTO_3D via
    // kEasingFunctions. Stored as the enum so the component stays
    // trivially copyable and the curve survives a save/load round trip.
    IREasingFunctions easingFunction_ = IREasingFunctions::kLinearInterpolation;
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
        , easingFunction_{easingFunction} {}

    C_GotoEasing3D()
        : C_GotoEasing3D{IRMath::vec3{0.0f}, IRMath::vec3{0.0f}, 0.0f} {}
};

} // namespace IRComponents

#endif /* COMPONENT_GOTO_EASING_3D_H */

#ifndef COMPONENT_GOTO_EASING_3D_H
#define COMPONENT_GOTO_EASING_3D_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/common/components/component_position_3d.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_GotoEasing3D {
    C_Position3D startPos_;
    C_Position3D endPos_;
    int durationFrames_;
    int currentFrame_;
    GLMEasingFunction easingFunction_;
    bool done_ = false;

    C_GotoEasing3D(C_Position3D start, C_Position3D end, float durationSeconds,
                   IREasingFunctions easingFunction = IREasingFunctions::kLinearInterpolation)
        : startPos_{start}, endPos_{end},
          durationFrames_{IRMath::secondsToFrames<IRConstants::kFPS>(durationSeconds)},
          currentFrame_{0}, easingFunction_{kEasingFunctions.at(easingFunction)} {}

    C_GotoEasing3D() : C_GotoEasing3D{C_Position3D{}, C_Position3D{}, 0.0f} {}
};

} // namespace IRComponents

#endif /* COMPONENT_GOTO_EASING_3D_H */

#ifndef COMPONENT_ANIM_MOTION_COLOR_SHIFT_H
#define COMPONENT_ANIM_MOTION_COLOR_SHIFT_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_AnimMotionColorShift {
    Color motionColor_;
    float fadeInSpeed_;
    float fadeOutSpeed_;
    float currentBlend_;

    C_AnimMotionColorShift(
        Color motionColor,
        float fadeInSpeed,
        float fadeOutSpeed
    )
        : motionColor_{motionColor}
        , fadeInSpeed_{fadeInSpeed}
        , fadeOutSpeed_{fadeOutSpeed}
        , currentBlend_{0.0f} {}

    C_AnimMotionColorShift()
        : C_AnimMotionColorShift(
              IRColors::kWhite,
              4.0f,
              2.0f) {}
};

} // namespace IRComponents

#endif /* COMPONENT_ANIM_MOTION_COLOR_SHIFT_H */

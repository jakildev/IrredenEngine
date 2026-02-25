#ifndef COMPONENT_ANIM_CLIP_COLOR_TRACK_H
#define COMPONENT_ANIM_CLIP_COLOR_TRACK_H

#include <irreden/ir_math.hpp>
#include <irreden/update/components/component_action_animation.hpp>

#include <array>

using namespace IRMath;

namespace IRComponents {

enum AnimColorTrackMode {
    ANIM_COLOR_TRACK_ABSOLUTE = 0,
    ANIM_COLOR_TRACK_HSV_OFFSET,
};

struct AnimPhaseColor {
    Color startColor_;
    Color endColor_;
    IREasingFunctions easingFunction_;

    AnimPhaseColor(
        Color startColor,
        Color endColor,
        IREasingFunctions easingFunction
    )
        : startColor_{startColor}
        , endColor_{endColor}
        , easingFunction_{easingFunction} {}

    AnimPhaseColor()
        : AnimPhaseColor(
              IRColors::kWhite,
              IRColors::kWhite,
              IREasingFunctions::kLinearInterpolation) {}
};

// HSV offsets applied relative to entity's base color.
// hue wraps, saturation and value are clamped to [0,1].
struct AnimPhaseColorMod {
    ColorHSV startMod_;
    ColorHSV endMod_;
    IREasingFunctions easingFunction_;

    AnimPhaseColorMod(
        ColorHSV startMod,
        ColorHSV endMod,
        IREasingFunctions easingFunction
    )
        : startMod_{startMod}
        , endMod_{endMod}
        , easingFunction_{easingFunction} {}

    AnimPhaseColorMod()
        : startMod_{0.0f, 0.0f, 0.0f, 0.0f}
        , endMod_{0.0f, 0.0f, 0.0f, 0.0f}
        , easingFunction_{IREasingFunctions::kLinearInterpolation} {}
};

struct C_AnimClipColorTrack {
    AnimColorTrackMode mode_ = ANIM_COLOR_TRACK_ABSOLUTE;

    // Absolute mode data
    std::array<AnimPhaseColor, kMaxActionAnimationPhases> phaseColors_;
    Color idleColor_;

    // HSV offset mode data
    std::array<AnimPhaseColorMod, kMaxActionAnimationPhases> phaseMods_;
    ColorHSV idleMod_;

    int phaseCount_ = 0;

    C_AnimClipColorTrack()
        : mode_{ANIM_COLOR_TRACK_ABSOLUTE}
        , phaseColors_{}
        , idleColor_{IRColors::kWhite}
        , phaseMods_{}
        , idleMod_{0.0f, 0.0f, 0.0f, 0.0f}
        , phaseCount_{0} {}

    void addPhaseColor(const AnimPhaseColor &pc) {
        if (phaseCount_ < kMaxActionAnimationPhases) {
            phaseColors_[phaseCount_++] = pc;
        }
    }

    void addPhaseMod(const AnimPhaseColorMod &pm) {
        if (phaseCount_ < kMaxActionAnimationPhases) {
            phaseMods_[phaseCount_++] = pm;
        }
    }
};

} // namespace IRComponents

#endif /* COMPONENT_ANIM_CLIP_COLOR_TRACK_H */

#ifndef COMPONENT_TRIGGER_GLOW_H
#define COMPONENT_TRIGGER_GLOW_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

// Triggered color pulse:
// on contact-enter, voxels switch to targetColor and then ease back to baseColor.
struct C_TriggerGlow {
    Color baseColor_;
    bool baseColorInitialized_;

    Color targetColor_;
    float holdSeconds_;
    float fadeSeconds_;
    IREasingFunctions easingFunction_;
    bool triggerOnContactEnter_;

    float elapsedSeconds_;
    bool active_;

    C_TriggerGlow(Color targetColor, float holdSeconds, float fadeSeconds,
                  IREasingFunctions easingFunction, bool triggerOnContactEnter = true)
        : baseColor_{IRColors::kWhite}, baseColorInitialized_{false}, targetColor_{targetColor},
          holdSeconds_{holdSeconds}, fadeSeconds_{fadeSeconds}, easingFunction_{easingFunction},
          triggerOnContactEnter_{triggerOnContactEnter}, elapsedSeconds_{0.0f}, active_{false} {}

    C_TriggerGlow()
        : C_TriggerGlow(IRColors::kWhite, 0.03f, 0.22f, IREasingFunctions::kCubicEaseOut, true) {}
};

} // namespace IRComponents

#endif /* COMPONENT_TRIGGER_GLOW_H */

#ifndef COMPONENT_SPAWN_GLOW_H
#define COMPONENT_SPAWN_GLOW_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

// Auto-activated glow: voxels start at targetColor and fade back to baseColor.
// Unlike C_TriggerGlow, no contact event is needed -- the glow fires on first tick.
struct C_SpawnGlow {
    Color baseColor_;
    Color targetColor_;
    float holdSeconds_;
    float fadeSeconds_;
    IREasingFunctions easingFunction_;

    float elapsedSeconds_;
    bool active_;

    C_SpawnGlow(
        Color baseColor,
        Color targetColor,
        float holdSeconds,
        float fadeSeconds,
        IREasingFunctions easingFunction
    )
        : baseColor_{baseColor}
        , targetColor_{targetColor}
        , holdSeconds_{holdSeconds}
        , fadeSeconds_{fadeSeconds}
        , easingFunction_{easingFunction}
        , elapsedSeconds_{0.0f}
        , active_{true} {}

    C_SpawnGlow()
        : C_SpawnGlow(
              IRColors::kWhite,
              IRColors::kWhite,
              0.03f,
              0.22f,
              IREasingFunctions::kCubicEaseOut) {}
};

} // namespace IRComponents

#endif /* COMPONENT_SPAWN_GLOW_H */

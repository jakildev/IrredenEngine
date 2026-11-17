#ifndef COMPONENT_POST_EFFECTS_STATE_H
#define COMPONENT_POST_EFFECTS_STATE_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

// How the bloom glow joins the picture. `ADD` sums and clips at white;
// `SCREEN` is `1 - (1 - base)(1 - glow)`, which never clips.
enum class BloomBlend : int { ADD = 0, SCREEN = 1 };

// World-scoped singleton driving `FRAMEBUFFER_POST_EFFECTS`. Plain data:
// validation, the frame plan and the Lua surface live in
// `IRPrefab::PostEffects` (`post_effects.hpp`), the only writer that checks
// the ranges below. Parameters keep their values while their switch is off.
//
// Ranges a stored value satisfies (every float finite):
// `bloomThreshold_`, `bloomKnee_`, `bloomScatter_` in [0, 1];
// `bloomIntensity_` in [0, 8]; `bloomLevels_` in [1, 6];
// `blurRadius_` in [0, 32] framebuffer pixels; `exposureStops_` in [-8, 8];
// `lift_` per channel in [-1, 1]; `gamma_` in [0.1, 8]; `gain_` in [0, 8];
// `saturation_`, `contrast_` in [0, 4].
struct C_PostEffectsState {
    bool bloomEnabled_ = false;
    // Brightness is a texel's largest channel.
    float bloomThreshold_ = 0.7f;
    float bloomKnee_ = 0.2f;
    float bloomIntensity_ = 1.0f;
    float bloomScatter_ = 0.7f;
    int bloomLevels_ = 5;
    BloomBlend bloomBlend_ = BloomBlend::ADD;

    bool blurEnabled_ = false;
    // Two standard deviations of the Gaussian.
    float blurRadius_ = 2.0f;

    bool gradeEnabled_ = false;
    float exposureStops_ = 0.0f;
    IRMath::vec3 lift_ = IRMath::vec3(0.0f);
    IRMath::vec3 gamma_ = IRMath::vec3(1.0f);
    IRMath::vec3 gain_ = IRMath::vec3(1.0f);
    float saturation_ = 1.0f;
    float contrast_ = 1.0f;
};

} // namespace IRComponents

#endif /* COMPONENT_POST_EFFECTS_STATE_H */

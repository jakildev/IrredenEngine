#ifndef COMPONENT_SPRING_PLATFORM_H
#define COMPONENT_SPRING_PLATFORM_H

#include <irreden/ir_math.hpp>

using IRMath::ColorHSV;
using IRMath::vec3;

namespace IRComponents {

enum SpringPlatformState {
    SPRING_IDLE = 0,
    SPRING_CATCHING,
    SPRING_LOCKED,
    SPRING_ANTICIPATING,
    SPRING_LAUNCHING,
    SPRING_REBOUNDING,
};

struct C_SpringPlatform {
    vec3 origin_;
    bool originInitialized_;

    vec3 direction_;
    float stiffness_;
    float damping_;
    float length_;
    float lockRatio_;
    float overshootRatio_;
    float absorptionRatio_;

    float displacement_;
    float springVelocity_;

    SpringPlatformState state_;
    bool launchRequested_;
    bool catchReachedMax_;

    int maxLaunchOscillations_;
    int maxCatchOscillations_;
    int oscillationCount_;
    float settleSpeed_;
    float previousDisplacement_;

    float loadLeadSeconds_;

    float colorProgress_;
    ColorHSV lockColorShift_;
    ColorHSV releaseColorShift_;
    float colorMinValue_;
    float colorMinSaturation_;

    C_SpringPlatform(
        float stiffness,
        float damping,
        float length,
        float lockRatio,
        float overshootRatio,
        float absorptionRatio,
        int maxLaunchOscillations,
        int maxCatchOscillations,
        float settleSpeed,
        float loadLeadSeconds,
        vec3 direction,
        ColorHSV lockColorShift,
        ColorHSV releaseColorShift = ColorHSV{0.0f, 0.0f, 0.0f, 0.0f},
        float colorMinValue = 0.0f,
        float colorMinSaturation = 0.0f
    )
        : origin_{vec3(0.0f)}
        , originInitialized_{false}
        , direction_{direction}
        , stiffness_{stiffness}
        , damping_{damping}
        , length_{length}
        , lockRatio_{IRMath::clamp(lockRatio, 0.0f, 1.0f)}
        , overshootRatio_{IRMath::clamp(overshootRatio, 0.0f, 0.5f)}
        , absorptionRatio_{absorptionRatio}
        , displacement_{0.0f}
        , springVelocity_{0.0f}
        , state_{SPRING_IDLE}
        , launchRequested_{false}
        , catchReachedMax_{false}
        , maxLaunchOscillations_{maxLaunchOscillations}
        , maxCatchOscillations_{maxCatchOscillations}
        , oscillationCount_{0}
        , settleSpeed_{settleSpeed}
        , previousDisplacement_{0.0f}
        , loadLeadSeconds_{loadLeadSeconds}
        , colorProgress_{0.0f}
        , lockColorShift_{lockColorShift}
        , releaseColorShift_{releaseColorShift}
        , colorMinValue_{IRMath::clamp(colorMinValue, 0.0f, 1.0f)}
        , colorMinSaturation_{IRMath::clamp(colorMinSaturation, 0.0f, 1.0f)} {}

    C_SpringPlatform()
        : C_SpringPlatform(
              200.0f,
              10.0f,
              8.0f,
              0.7f,
              0.2f,
              1.0f,
              3,
              2,
              0.5f,
              0.15f,
              vec3(0.0f, 0.0f, 1.0f),
              ColorHSV{0.0f, 0.0f, -0.3f, 0.0f}
          ) {}
};

} // namespace IRComponents

#endif /* COMPONENT_SPRING_PLATFORM_H */

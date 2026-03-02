#ifndef COMPONENT_PARTICLE_BURST_H
#define COMPONENT_PARTICLE_BURST_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_ParticleBurst {
    int count_;
    int lifetime_;
    float speed_;
    float upwardAcceleration_;
    float dragScaleX_;
    float dragScaleY_;
    float dragScaleZ_;
    float spawnOffsetZ_;
    float isoDepthOffset_;

    float xySpeedRatio_          = 0.15f;
    float zSpeedRatio_           = 1.0f;
    float zVarianceRatio_        = 0.3f;
    float pDragPerSecond_        = 4.5f;
    float pDriftDelaySeconds_    = 1.2f;
    float pDriftUpAccelPerSec_   = 20.0f;
    float pDragMinSpeed_         = 0.01f;
    float pHoverDurationSec_     = 1.5f;
    float pHoverOscSpeed_        = 5.0f;
    float pHoverOscAmplitude_    = 3.0f;
    float pHoverBlendSec_        = 0.5f;
    IREasingFunctions pHoverBlendEasing_ = IREasingFunctions::kLinearInterpolation;

    float hoverStartVariance_    = 0.0f;
    float hoverDurationVariance_ = 0.0f;
    float hoverAmplitudeVariance_ = 0.0f;
    float hoverSpeedVariance_    = 0.0f;

    bool glowEnabled_            = false;
    Color glowColor_             = IRColors::kWhite;
    float glowHoldSeconds_       = 0.03f;
    float glowFadeSeconds_       = 0.22f;
    IREasingFunctions glowEasing_ = IREasingFunctions::kCubicEaseOut;

    // Downward mode: particles launch down, affected by gravity, with hover oscillation
    bool gravityEnabled_        = false;
    bool downward_              = false;

    C_ParticleBurst(
        int count,
        int lifetime,
        float speed,
        float upwardAcceleration = 0.0f,
        float dragScaleX = 1.0f,
        float dragScaleY = 1.0f,
        float dragScaleZ = 1.0f,
        float spawnOffsetZ = 0.0f,
        float isoDepthOffset = 0.0f
    )
        : count_{count}
        , lifetime_{lifetime}
        , speed_{speed}
        , upwardAcceleration_{upwardAcceleration}
        , dragScaleX_{dragScaleX}
        , dragScaleY_{dragScaleY}
        , dragScaleZ_{dragScaleZ}
        , spawnOffsetZ_{spawnOffsetZ}
        , isoDepthOffset_{isoDepthOffset} {}

    C_ParticleBurst()
        : C_ParticleBurst(6, 40, 12.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f) {}
};

} // namespace IRComponents

#endif /* COMPONENT_PARTICLE_BURST_H */

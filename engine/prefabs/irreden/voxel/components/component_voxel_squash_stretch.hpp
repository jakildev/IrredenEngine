#ifndef COMPONENT_VOXEL_SQUASH_STRETCH_H
#define COMPONENT_VOXEL_SQUASH_STRETCH_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

/// Physics-driven squash & stretch: velocity→stretch (momentum), acceleration→squash (force).
/// Generic: works with any C_VoxelSetNew + C_Velocity3D.
/// Optional: impact boost, spring-state coupling (when C_RhythmicLaunch + C_ContactEvent).
struct C_VoxelSquashStretch {
    // Stretch: driven by velocity magnitude (momentum)
    float stretchStrength_;   // elongation along velocity at max; e.g. 0.3 = 30%
    float stretchSpeedRef_;   // speed (blocks/sec) at which stretch is full

    // Squash: driven by acceleration magnitude (force)
    float squashStrength_;   // compression along accel at max; e.g. 0.25 = 25%
    float squashAccelRef_;   // accel (blocks/sec²) at which squash is full

    // Volume preservation: scale perpendicular by 1/sqrt(primary) when true
    bool volumePreserve_;

    float roundness_;        // 0 = sharp, 1 = soft falloff toward edges

    // Optional impact boost (when C_RhythmicLaunch + C_ContactEvent present)
    float impactBoost_;        // multiplier for squash on contact; 0 = none, 1.5 = 50% extra
    float impactSquashZ_;      // Z scale on landing; 1 = none, 0.55 = squash to 55%
    float impactExpandXY_;     // XY expansion on landing; 1 = none, 1.25 = 25% wider
    float impactDurationSec_;
    float impactElapsedSec_;

    // Optional spring coupling: squash bias when platform is CATCHING/ANTICIPATING
    float springBias_;        // 0 = off; >0 = extra squash when platform compressing
    bool useSpringBias_;      // whether to query platform spring state

    // Smoothing (0 = off; >0 reduces jitter)
    float smoothing_;
    float smoothedSpeed_;
    vec3 smoothedVelDir_;
    vec3 prevVelocity_;       // for acceleration (written by system)

    C_VoxelSquashStretch(
        float stretchStrength,
        float squashStrength,
        float stretchSpeedRef,
        float squashAccelRef,
        bool volumePreserve,
        float roundness,
        float impactBoost,
        float impactSquashZ,
        float impactExpandXY,
        float impactDurationSec,
        float springBias,
        bool useSpringBias,
        float smoothing
    )
        : stretchStrength_{stretchStrength}
        , stretchSpeedRef_{stretchSpeedRef}
        , squashStrength_{squashStrength}
        , squashAccelRef_{squashAccelRef}
        , volumePreserve_{volumePreserve}
        , roundness_{roundness}
        , impactBoost_{impactBoost}
        , impactSquashZ_{impactSquashZ}
        , impactExpandXY_{impactExpandXY}
        , impactDurationSec_{impactDurationSec}
        , impactElapsedSec_{impactDurationSec}
        , springBias_{springBias}
        , useSpringBias_{useSpringBias}
        , smoothing_{smoothing}
        , smoothedSpeed_{0.0f}
        , smoothedVelDir_{vec3(0.0f, 0.0f, 0.0f)}
        , prevVelocity_{vec3(0.0f, 0.0f, 0.0f)} {}

    /// Backward-compatible 6-param
    C_VoxelSquashStretch(
        float stretchStrength,
        float squashStrength,
        float maxSpeedRef,
        float roundness,
        float impactSquashZ,
        float impactDurationSec
    )
        : stretchStrength_{stretchStrength}
        , stretchSpeedRef_{maxSpeedRef}
        , squashStrength_{squashStrength}
        , squashAccelRef_{150.0f}
        , volumePreserve_{true}
        , roundness_{roundness}
        , impactBoost_{1.5f}
        , impactSquashZ_{impactSquashZ}
        , impactExpandXY_{1.2f}
        , impactDurationSec_{impactDurationSec}
        , impactElapsedSec_{impactDurationSec}
        , springBias_{0.2f}
        , useSpringBias_{true}
        , smoothing_{0.06f}
        , smoothedSpeed_{0.0f}
        , smoothedVelDir_{vec3(0.0f, 0.0f, 0.0f)}
        , prevVelocity_{vec3(0.0f, 0.0f, 0.0f)} {}

    C_VoxelSquashStretch()
        : C_VoxelSquashStretch(0.2f, 0.15f, 50.0f, 0.6f, 0.75f, 0.12f) {}
};

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_SQUASH_STRETCH_H */

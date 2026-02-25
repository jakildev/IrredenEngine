#ifndef COMPONENT_RHYTHMIC_LAUNCH_H
#define COMPONENT_RHYTHMIC_LAUNCH_H

#include <irreden/ir_math.hpp>

using IRMath::vec3;

namespace IRComponents {

struct C_RhythmicLaunch {
    double periodSeconds_;
    vec3 impulseVelocity_;
    float restOffsetZ_;
    double elapsedSeconds_;
    bool grounded_;

    C_RhythmicLaunch(
        float periodSeconds,
        vec3 impulseVelocity,
        float restOffsetZ,
        float initialElapsed = 0.0f,
        bool startGrounded = true
    )
        : periodSeconds_{static_cast<double>(periodSeconds)}
        , impulseVelocity_{impulseVelocity}
        , restOffsetZ_{restOffsetZ}
        , elapsedSeconds_{static_cast<double>(initialElapsed)}
        , grounded_{startGrounded} {}

    C_RhythmicLaunch()
        : C_RhythmicLaunch(1.0f, vec3(0.0f, 0.0f, -50.0f), 6.0f) {}
};

} // namespace IRComponents

#endif /* COMPONENT_RHYTHMIC_LAUNCH_H */

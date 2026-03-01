#ifndef COMPONENT_RHYTHMIC_LAUNCH_H
#define COMPONENT_RHYTHMIC_LAUNCH_H

#include <irreden/ir_math.hpp>
#include <irreden/entity/ir_entity_types.hpp>

using IRMath::vec3;

namespace IRComponents {

struct C_RhythmicLaunch {
    double periodSeconds_;
    vec3 impulseVelocity_;
    float restOffsetZ_;
    double elapsedSeconds_;
    bool grounded_;
    int32_t maxLaunches_;   // <=0 means unlimited
    int32_t launchCount_;
    IREntity::EntityId lastPlatformEntity_;
    bool frozen_;
    vec3 frozenPos_;

    C_RhythmicLaunch(
        float periodSeconds,
        vec3 impulseVelocity,
        float restOffsetZ,
        float initialElapsed = 0.0f,
        bool startGrounded = true,
        int32_t maxLaunches = -1
    )
        : periodSeconds_{static_cast<double>(periodSeconds)}
        , impulseVelocity_{impulseVelocity}
        , restOffsetZ_{restOffsetZ}
        , elapsedSeconds_{static_cast<double>(initialElapsed)}
        , grounded_{startGrounded}
        , maxLaunches_{maxLaunches}
        , launchCount_{0}
        , lastPlatformEntity_{IREntity::kNullEntity}
        , frozen_{false}
        , frozenPos_{vec3(0.0f)} {}

    C_RhythmicLaunch()
        : C_RhythmicLaunch(1.0f, vec3(0.0f, 0.0f, -50.0f), 6.0f) {}

    bool atMaxLaunches() const {
        return maxLaunches_ > 0 && launchCount_ >= maxLaunches_;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_RHYTHMIC_LAUNCH_H */

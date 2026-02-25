#ifndef COMPONENT_ANIMATION_CLIP_H
#define COMPONENT_ANIMATION_CLIP_H

#include <irreden/update/components/component_action_animation.hpp>

#include <array>

namespace IRComponents {

struct C_AnimationClip {
    std::array<ActionAnimationPhase, kMaxActionAnimationPhases> phases_;
    int phaseCount_ = 0;
    int actionPhaseIndex_ = -1;

    C_AnimationClip()
        : phases_{}
        , phaseCount_{0}
        , actionPhaseIndex_{-1} {}

    void addPhase(const ActionAnimationPhase &phase) {
        if (phaseCount_ < kMaxActionAnimationPhases) {
            phases_[phaseCount_++] = phase;
        }
    }
};

} // namespace IRComponents

#endif /* COMPONENT_ANIMATION_CLIP_H */

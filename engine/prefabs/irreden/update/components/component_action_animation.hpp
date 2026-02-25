#ifndef COMPONENT_ACTION_ANIMATION_H
#define COMPONENT_ACTION_ANIMATION_H

#include <irreden/ir_math.hpp>
#include <irreden/entity/ir_entity_types.hpp>

#include <array>

using namespace IRMath;

namespace IRComponents {

static constexpr int kMaxActionAnimationPhases = 8;
static constexpr int kMaxAnimationBindings = 4;

enum ActionAnimationTriggerMode {
    ANIM_TRIGGER_CONTACT_ENTER = 0,
    ANIM_TRIGGER_TIMER_SYNC,
    ANIM_TRIGGER_KEYPRESS,
    ANIM_TRIGGER_MANUAL,
};

struct ActionAnimationPhase {
    double durationSeconds_;
    float startDisplacement_;
    float endDisplacement_;
    IREasingFunctions easingFunction_;

    ActionAnimationPhase(
        double durationSeconds,
        float startDisplacement,
        float endDisplacement,
        IREasingFunctions easingFunction
    )
        : durationSeconds_{durationSeconds}
        , startDisplacement_{startDisplacement}
        , endDisplacement_{endDisplacement}
        , easingFunction_{easingFunction} {}

    ActionAnimationPhase()
        : ActionAnimationPhase(0.1, 0.0f, 0.0f, IREasingFunctions::kLinearInterpolation) {}
};

struct AnimationBinding {
    ActionAnimationTriggerMode trigger_ = ANIM_TRIGGER_MANUAL;
    IREntity::EntityId clipEntity_ = IREntity::kNullEntity;
    double timerSyncLeadSeconds_ = 0.0;
    bool canInterrupt_ = false;

    AnimationBinding() = default;

    AnimationBinding(
        ActionAnimationTriggerMode trigger,
        IREntity::EntityId clipEntity,
        double timerSyncLeadSeconds,
        bool canInterrupt
    )
        : trigger_{trigger}
        , clipEntity_{clipEntity}
        , timerSyncLeadSeconds_{timerSyncLeadSeconds}
        , canInterrupt_{canInterrupt} {}
};

struct C_ActionAnimation {
    vec3 direction_;
    std::array<AnimationBinding, kMaxAnimationBindings> bindings_;
    int bindingCount_ = 0;

    vec3 origin_;
    bool originInitialized_ = false;
    float currentDisplacement_ = 0.0f;

    IREntity::EntityId activeClip_ = IREntity::kNullEntity;
    int activeBindingIndex_ = -1;
    int currentPhase_ = 0;
    double phaseElapsed_ = 0.0;
    bool actionFired_ = false;

    float phaseStartOverride_ = 0.0f;
    bool hasPhaseStartOverride_ = false;

    C_ActionAnimation()
        : direction_{vec3(0.0f, 0.0f, -1.0f)}
        , bindings_{}
        , bindingCount_{0}
        , origin_{vec3(0.0f)}
        , originInitialized_{false}
        , currentDisplacement_{0.0f}
        , activeClip_{IREntity::kNullEntity}
        , activeBindingIndex_{-1}
        , currentPhase_{0}
        , phaseElapsed_{0.0}
        , actionFired_{false}
        , phaseStartOverride_{0.0f}
        , hasPhaseStartOverride_{false} {}

    int addBinding(const AnimationBinding &binding) {
        if (bindingCount_ < kMaxAnimationBindings) {
            bindings_[bindingCount_] = binding;
            return bindingCount_++;
        }
        return -1;
    }

    bool isPlaying() const {
        return activeClip_ != IREntity::kNullEntity;
    }

    void startClip(int bindingIndex) {
        activeClip_ = bindings_[bindingIndex].clipEntity_;
        activeBindingIndex_ = bindingIndex;
        currentPhase_ = 0;
        phaseElapsed_ = 0.0;
        actionFired_ = false;
        hasPhaseStartOverride_ = false;
    }

    void startClip(int bindingIndex, float startOverride) {
        activeClip_ = bindings_[bindingIndex].clipEntity_;
        activeBindingIndex_ = bindingIndex;
        currentPhase_ = 0;
        phaseElapsed_ = 0.0;
        actionFired_ = false;
        phaseStartOverride_ = startOverride;
        hasPhaseStartOverride_ = true;
    }

    void stopClip() {
        activeClip_ = IREntity::kNullEntity;
        activeBindingIndex_ = -1;
        currentPhase_ = 0;
        phaseElapsed_ = 0.0;
        hasPhaseStartOverride_ = false;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_ACTION_ANIMATION_H */

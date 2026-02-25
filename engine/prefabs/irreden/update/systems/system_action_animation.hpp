#ifndef SYSTEM_ACTION_ANIMATION_H
#define SYSTEM_ACTION_ANIMATION_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_action_animation.hpp>
#include <irreden/update/components/component_animation_clip.hpp>
#include <irreden/update/components/component_rhythmic_launch.hpp>

#include <unordered_map>
#include <cmath>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<ACTION_ANIMATION> {
    static SystemId create() {
        static std::unordered_map<IREntity::EntityId, const C_AnimationClip*> clipCache;

        return createSystem<C_ActionAnimation, C_Position3D, C_ContactEvent>(
            "ActionAnimation",
            [](C_ActionAnimation &anim,
               C_Position3D &position,
               const C_ContactEvent &contact) {
                if (anim.bindingCount_ <= 0) return;

                if (!anim.originInitialized_) {
                    anim.origin_ = position.pos_;
                    anim.originInitialized_ = true;
                }

                auto fetchClip = [](IREntity::EntityId id) -> const C_AnimationClip* {
                    if (id == IREntity::kNullEntity) return nullptr;
                    auto it = clipCache.find(id);
                    if (it != clipCache.end()) return it->second;
                    auto opt = IREntity::getComponentOptional<C_AnimationClip>(id);
                    if (!opt.has_value()) return nullptr;
                    clipCache[id] = opt.value();
                    return opt.value();
                };

                auto checkTrigger = [&](const AnimationBinding &binding) -> bool {
                    switch (binding.trigger_) {
                    case ANIM_TRIGGER_CONTACT_ENTER:
                        return contact.entered_;

                    case ANIM_TRIGGER_TIMER_SYNC:
                        if (contact.touching_ &&
                            contact.otherEntity_ != IREntity::kNullEntity) {
                            auto launchOpt =
                                IREntity::getComponentOptional<C_RhythmicLaunch>(
                                    contact.otherEntity_);
                            if (launchOpt.has_value()) {
                                const auto &launch = *launchOpt.value();
                                if (launch.periodSeconds_ <= 0.0) {
                                    return false;
                                }
                                double elapsedWrapped = std::fmod(
                                    launch.elapsedSeconds_,
                                    launch.periodSeconds_
                                );
                                if (elapsedWrapped < 0.0) {
                                    elapsedWrapped += launch.periodSeconds_;
                                }
                                double timeUntilFire = launch.periodSeconds_ - elapsedWrapped;
                                return timeUntilFire <= binding.timerSyncLeadSeconds_ &&
                                       timeUntilFire >= 0.0;
                            }
                        }
                        return false;

                    case ANIM_TRIGGER_KEYPRESS:
                    case ANIM_TRIGGER_MANUAL:
                    default:
                        return false;
                    }
                };

                const double dt = static_cast<double>(
                    IRTime::deltaTime(IRTime::UPDATE));

                // --- Trigger check ---
                for (int i = 0; i < anim.bindingCount_; ++i) {
                    if (anim.isPlaying() && i == anim.activeBindingIndex_)
                        continue;

                    const auto &binding = anim.bindings_[i];

                    if (anim.isPlaying() && !binding.canInterrupt_)
                        continue;

                    if (!checkTrigger(binding))
                        continue;

                    const C_AnimationClip *clip = fetchClip(binding.clipEntity_);
                    if (!clip || clip->phaseCount_ <= 0)
                        continue;

                    if (anim.isPlaying()) {
                        anim.startClip(i, anim.currentDisplacement_);
                    } else {
                        anim.startClip(i);
                    }
                    break;
                }

                // --- Hold position when idle ---
                if (!anim.isPlaying()) {
                    position.pos_ = anim.origin_ +
                        anim.direction_ * anim.currentDisplacement_;
                    return;
                }

                // --- Phase playback ---
                const C_AnimationClip *clip = fetchClip(anim.activeClip_);
                if (!clip || clip->phaseCount_ <= 0) {
                    anim.stopClip();
                    return;
                }
                if (anim.currentPhase_ < 0 || anim.currentPhase_ >= clip->phaseCount_) {
                    anim.stopClip();
                    return;
                }

                anim.phaseElapsed_ += dt;

                const auto &phase = clip->phases_[anim.currentPhase_];
                double phaseDur = phase.durationSeconds_;
                if (phaseDur <= 0.0) phaseDur = 0.001;

                double t = anim.phaseElapsed_ / phaseDur;
                if (t > 1.0) t = 1.0;

                float startDisp = phase.startDisplacement_;
                if (anim.currentPhase_ == 0 && anim.hasPhaseStartOverride_) {
                    startDisp = anim.phaseStartOverride_;
                }

                float eased = kEasingFunctions.at(phase.easingFunction_)(
                    static_cast<float>(t));
                float displacement = IRMath::mix(
                    startDisp, phase.endDisplacement_, eased);

                anim.currentDisplacement_ = displacement;
                position.pos_ = anim.origin_ + anim.direction_ * displacement;

                // --- Phase completion ---
                if (t >= 1.0) {
                    if (clip->actionPhaseIndex_ >= 0 &&
                        anim.currentPhase_ == clip->actionPhaseIndex_) {
                        anim.actionFired_ = true;
                    }

                    if (anim.currentPhase_ == 0) {
                        anim.hasPhaseStartOverride_ = false;
                    }
                    anim.currentPhase_++;
                    anim.phaseElapsed_ = 0.0;

                    if (anim.currentPhase_ >= clip->phaseCount_) {
                        anim.currentDisplacement_ =
                            clip->phases_[clip->phaseCount_ - 1].endDisplacement_;
                        anim.stopClip();
                        position.pos_ = anim.origin_ +
                            anim.direction_ * anim.currentDisplacement_;
                        return;
                    }
                }
            },
            []() { clipCache.clear(); }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ACTION_ANIMATION_H */

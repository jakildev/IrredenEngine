#ifndef SYSTEM_SPRING_PLATFORM_H
#define SYSTEM_SPRING_PLATFORM_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_spring_platform.hpp>
#include <irreden/update/components/component_rhythmic_launch.hpp>

#include <cmath>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<SPRING_PLATFORM> {
    static SystemId create() {
        return createSystem<
            C_SpringPlatform,
            C_Position3D,
            C_Velocity3D,
            C_ContactEvent>(
            "SpringPlatform",
            [](C_SpringPlatform &spring,
               C_Position3D &position,
               C_Velocity3D &velocity,
               const C_ContactEvent &contact) {
                const float dt = IRTime::deltaTime(IRTime::UPDATE);
                if (dt <= 0.0f) return;

                if (!spring.originInitialized_) {
                    spring.origin_ = position.pos_;
                    spring.originInitialized_ = true;
                }

                spring.previousDisplacement_ = spring.displacement_;
                spring.displacement_ = IRMath::dot(
                    position.pos_ - spring.origin_, spring.direction_);

                const float lockPoint = spring.length_ * spring.lockRatio_;
                const float maxOvershoot = spring.length_ * spring.overshootRatio_;

                // ── Impact ──
                if (contact.entered_ &&
                    (spring.state_ == SPRING_IDLE ||
                     spring.state_ == SPRING_REBOUNDING)) {
                    float impactSpeed = 0.0f;
                    if (contact.otherEntity_ != IREntity::kNullEntity) {
                        auto velOpt = IREntity::getComponentOptional<C_Velocity3D>(
                            contact.otherEntity_);
                        if (velOpt.has_value()) {
                            vec3 bv = velOpt.value()->velocity_;
                            impactSpeed = std::abs(
                                IRMath::dot(bv, spring.direction_));
                        }
                    }
                    spring.springVelocity_ =
                        spring.absorptionRatio_ * impactSpeed;
                    spring.state_ = SPRING_CATCHING;
                    spring.catchReachedMax_ = false;
                    spring.oscillationCount_ = 0;
                }

                // ── CATCHING ──
                if (spring.state_ == SPRING_CATCHING) {
                    if (!spring.catchReachedMax_) {
                        // Phase 1: compress to max. No damping so the spring
                        // reaches full length (energy conservation).
                        float force = -spring.stiffness_ * spring.displacement_;
                        spring.springVelocity_ += force * dt;
                        if (spring.springVelocity_ <= 0.0f ||
                            spring.displacement_ >= spring.length_) {
                            spring.springVelocity_ = 0.0f;
                            spring.catchReachedMax_ = true;
                            spring.oscillationCount_ = 0;
                        }
                    } else {
                        float offset = spring.displacement_ - lockPoint;
                        float prevOffset =
                            spring.previousDisplacement_ - lockPoint;
                        if (prevOffset * offset < 0.0f) {
                            spring.oscillationCount_++;
                        }

                        bool settling = spring.maxCatchOscillations_ > 0 &&
                            spring.oscillationCount_ >= spring.maxCatchOscillations_;

                        if (settling) {
                            float rate = 1.0f - std::exp(-spring.settleSpeed_ * 60.0f * dt);
                            spring.displacement_ = IRMath::mix(
                                spring.displacement_, lockPoint, rate);
                            spring.springVelocity_ = 0.0f;
                            position.pos_ =
                                spring.origin_ + spring.direction_ * spring.displacement_;

                            if (std::abs(spring.displacement_ - lockPoint) < 0.01f) {
                                spring.displacement_ = lockPoint;
                                spring.state_ = SPRING_LOCKED;
                                position.pos_ =
                                    spring.origin_ + spring.direction_ * lockPoint;
                            }
                        } else {
                            float force = -spring.stiffness_ * offset;
                            spring.springVelocity_ += force * dt;
                            spring.springVelocity_ *= IRMath::max(
                                0.0f, 1.0f - spring.damping_ * dt);

                            if (spring.maxCatchOscillations_ <= 0 &&
                                std::abs(spring.springVelocity_) <=
                                    spring.settleSpeed_ &&
                                std::abs(offset) < 0.5f) {
                                spring.displacement_ = lockPoint;
                                spring.springVelocity_ = 0.0f;
                                spring.state_ = SPRING_LOCKED;
                            }
                        }
                    }
                }

                // ── LOCKED ──
                if (spring.state_ == SPRING_LOCKED) {
                    spring.springVelocity_ = 0.0f;
                    position.pos_ = spring.origin_ + spring.direction_ * lockPoint;
                    if (contact.touching_ &&
                        contact.otherEntity_ != IREntity::kNullEntity) {
                        auto launchOpt =
                            IREntity::getComponentOptional<C_RhythmicLaunch>(
                                contact.otherEntity_);
                        if (launchOpt.has_value()) {
                            const auto &launch = *launchOpt.value();
                            if (launch.periodSeconds_ > 0.0) {
                                double elapsed = std::fmod(
                                    launch.elapsedSeconds_,
                                    launch.periodSeconds_);
                                if (elapsed < 0.0)
                                    elapsed += launch.periodSeconds_;
                                double remaining =
                                    launch.periodSeconds_ - elapsed;
                                if (remaining <= spring.loadLeadSeconds_ &&
                                    remaining >= 0.0) {
                                    spring.state_ = SPRING_ANTICIPATING;
                                }
                            }
                        }
                    }
                }

                // ── ANTICIPATING ──
                if (spring.state_ == SPRING_ANTICIPATING) {
                    float target = spring.length_;
                    float error = target - spring.displacement_;
                    spring.springVelocity_ +=
                        error * spring.stiffness_ * 2.0f * dt;
                    spring.springVelocity_ *= IRMath::max(
                        0.0f, 1.0f - spring.damping_ * 2.0f * dt);
                }

                // ── Launch ──
                if (spring.launchRequested_) {
                    float launchSpeed = std::sqrt(
                        spring.stiffness_ * spring.length_ * spring.length_);
                    spring.springVelocity_ = -launchSpeed;
                    spring.state_ = SPRING_LAUNCHING;
                    spring.launchRequested_ = false;
                    spring.oscillationCount_ = 0;
                }

                // ── LAUNCHING -> REBOUNDING transition ──
                if (spring.state_ == SPRING_LAUNCHING &&
                    spring.displacement_ <= 0.0f) {
                    spring.state_ = SPRING_REBOUNDING;
                }
                // ── Count oscillations during rebound ──
                // Skip the initial downward crossing (transition frame) so that
                // maxLaunchOscillations=1 means one visible bounce past origin.
                else if (spring.state_ == SPRING_REBOUNDING) {
                    if (spring.previousDisplacement_ *
                            spring.displacement_ < 0.0f) {
                        spring.oscillationCount_++;
                    }
                }

                // ── Check if launch settle should activate ──
                bool launchSettling = spring.state_ == SPRING_REBOUNDING &&
                    spring.maxLaunchOscillations_ > 0 &&
                    spring.oscillationCount_ >= spring.maxLaunchOscillations_;

                // ── LAUNCHING / REBOUNDING physics ──
                if ((spring.state_ == SPRING_LAUNCHING ||
                     spring.state_ == SPRING_REBOUNDING) && !launchSettling) {
                    float force =
                        -spring.stiffness_ * spring.displacement_;
                    spring.springVelocity_ += force * dt;
                    spring.springVelocity_ *= IRMath::max(
                        0.0f, 1.0f - spring.damping_ * dt);

                    if (spring.state_ == SPRING_REBOUNDING &&
                        spring.maxLaunchOscillations_ <= 0 &&
                        std::abs(spring.springVelocity_) <=
                            spring.settleSpeed_ &&
                        std::abs(spring.displacement_) < 0.5f) {
                        spring.displacement_ = 0.0f;
                        spring.springVelocity_ = 0.0f;
                        spring.state_ = SPRING_IDLE;
                        spring.oscillationCount_ = 0;
                        spring.catchReachedMax_ = false;
                        position.pos_ = spring.origin_;
                    }
                }

                // ── Clamp: downward at length, upward at overshoot ──
                if (spring.displacement_ > spring.length_ &&
                    spring.springVelocity_ > 0.0f) {
                    spring.springVelocity_ = 0.0f;
                }
                if (spring.displacement_ < -maxOvershoot &&
                    spring.springVelocity_ < 0.0f) {
                    spring.springVelocity_ = 0.0f;
                }

                // ── Launch settle ──
                if (launchSettling) {
                    float rate = 1.0f - std::exp(-spring.settleSpeed_ * 60.0f * dt);
                    spring.displacement_ = IRMath::mix(
                        spring.displacement_, 0.0f, rate);
                    spring.springVelocity_ = 0.0f;
                    position.pos_ =
                        spring.origin_ + spring.direction_ * spring.displacement_;

                    if (std::abs(spring.displacement_) < 0.01f) {
                        spring.displacement_ = 0.0f;
                        spring.state_ = SPRING_IDLE;
                        spring.oscillationCount_ = 0;
                        spring.catchReachedMax_ = false;
                        position.pos_ = spring.origin_;
                    }
                }

                // ── Output velocity ──
                velocity.velocity_ =
                    spring.direction_ * spring.springVelocity_;

                // ── Color progress ──
                switch (spring.state_) {
                case SPRING_CATCHING:
                    if (!spring.catchReachedMax_) {
                        spring.colorProgress_ = IRMath::clamp(
                            spring.displacement_ / spring.length_,
                            0.0f, 1.0f);
                    } else {
                        spring.colorProgress_ = 1.0f;
                    }
                    break;
                case SPRING_LOCKED:
                case SPRING_ANTICIPATING:
                    spring.colorProgress_ = 1.0f;
                    break;
                case SPRING_LAUNCHING:
                    spring.colorProgress_ = IRMath::clamp(
                        spring.displacement_ / spring.length_,
                        0.0f, 1.0f);
                    break;
                case SPRING_REBOUNDING:
                case SPRING_IDLE:
                default:
                    spring.colorProgress_ = 0.0f;
                    break;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SPRING_PLATFORM_H */

#ifndef SYSTEM_RHYTHMIC_LAUNCH_H
#define SYSTEM_RHYTHMIC_LAUNCH_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_rhythmic_launch.hpp>
#include <irreden/update/components/component_action_animation.hpp>

#include <unordered_map>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<RHYTHMIC_LAUNCH> {
    struct PlatformSnapshot {
        vec3 position_;
        vec3 velocity_;
    };

    static SystemId create() {
        static std::unordered_map<IREntity::EntityId, PlatformSnapshot> platformCache;

        return createSystem<
            C_Position3D,
            C_PositionGlobal3D,
            C_Velocity3D,
            C_ContactEvent,
            C_RhythmicLaunch>(
            "RhythmicLaunch",
            [](C_Position3D &position,
               C_PositionGlobal3D &globalPosition,
               C_Velocity3D &velocity,
               const C_ContactEvent &contact,
               C_RhythmicLaunch &launch) {
                const float dt = IRTime::deltaTime(IRTime::UPDATE);

                auto fetchPlatform =
                    [](IREntity::EntityId id) -> PlatformSnapshot {
                    auto it = platformCache.find(id);
                    if (it != platformCache.end()) {
                        return it->second;
                    }
                    PlatformSnapshot snap{vec3(0.0f), vec3(0.0f)};
                    if (!IREntity::entityExists(id)) {
                        return snap;
                    }
                    auto posOpt =
                        IREntity::getComponentOptional<C_Position3D>(id);
                    if (posOpt.has_value()) {
                        snap.position_ = posOpt.value()->pos_;
                    }
                    auto velOpt =
                        IREntity::getComponentOptional<C_Velocity3D>(id);
                    if (velOpt.has_value()) {
                        snap.velocity_ = velOpt.value()->velocity_;
                    }
                    platformCache[id] = snap;
                    return snap;
                };

                // Timer always ticks (grounded and in-flight) so the
                // total period equals the designed note interval.
                launch.elapsedSeconds_ += dt;

                // --- Landing ---
                if (contact.entered_ && !launch.grounded_) {
                    launch.grounded_ = true;
                    PlatformSnapshot plat = fetchPlatform(contact.otherEntity_);
                    position.pos_.z = plat.position_.z - launch.restOffsetZ_;
                    globalPosition.pos_ = position.pos_;
                    velocity.velocity_ = plat.velocity_;
                }

                // --- Grounded: contact constraint + carry + launch ---
                if (launch.grounded_) {
                    if (velocity.velocity_.z > 0.0f) {
                        velocity.velocity_.z = 0.0f;
                    }

                    if (contact.touching_ &&
                        contact.otherEntity_ != IREntity::kNullEntity) {
                        PlatformSnapshot plat =
                            fetchPlatform(contact.otherEntity_);
                        position.pos_.z =
                            plat.position_.z - launch.restOffsetZ_;
                        globalPosition.pos_ = position.pos_;
                        velocity.velocity_ = plat.velocity_;
                    }
                }

                // --- Launch ---
                // When the platform has C_ActionAnimation, launch is
                // driven by the animation's action point (actionFired_).
                // The raw timer fallback is suppressed so the animation
                // can complete its anticipation before the impulse fires.
                bool launchNow = false;
                bool platformHasAnim = false;
                if (contact.touching_ &&
                    contact.otherEntity_ != IREntity::kNullEntity) {
                    auto animOpt =
                        IREntity::getComponentOptional<C_ActionAnimation>(
                            contact.otherEntity_);
                    if (animOpt.has_value()) {
                        platformHasAnim = true;
                        if (animOpt.value()->actionFired_) {
                            launchNow = true;
                            animOpt.value()->actionFired_ = false;
                        }
                    }
                }
                if (!launchNow && !platformHasAnim &&
                    launch.elapsedSeconds_ >= launch.periodSeconds_) {
                    launchNow = true;
                }
                if (launchNow) {
                    velocity.velocity_ = launch.impulseVelocity_;
                    launch.grounded_ = false;
                    if (launch.elapsedSeconds_ >= launch.periodSeconds_) {
                        launch.elapsedSeconds_ -= launch.periodSeconds_;
                    }
                }
            },
            []() { platformCache.clear(); }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_RHYTHMIC_LAUNCH_H */

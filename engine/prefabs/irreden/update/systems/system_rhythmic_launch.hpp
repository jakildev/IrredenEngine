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
#include <irreden/update/components/component_spring_platform.hpp>

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

                if (launch.frozen_) {
                    position.pos_ = launch.frozenPos_;
                    globalPosition.pos_ = launch.frozenPos_;
                    velocity.velocity_ = vec3(0.0f);
                    return;
                }

                if (launch.atMaxLaunches()) {
                    if (launch.grounded_) return;
                    if (velocity.velocity_.z >= 0.0f) {
                        launch.frozen_ = true;
                        launch.frozenPos_ = position.pos_;
                        velocity.velocity_ = vec3(0.0f);
                    }
                    return;
                }

                launch.elapsedSeconds_ += dt;

                // ── Landing ──
                if (contact.entered_ && !launch.grounded_) {
                    launch.grounded_ = true;
                    launch.lastPlatformEntity_ = contact.otherEntity_;
                    PlatformSnapshot plat = fetchPlatform(contact.otherEntity_);
                    position.pos_.z = plat.position_.z - launch.restOffsetZ_;
                    globalPosition.pos_ = position.pos_;
                    velocity.velocity_ = plat.velocity_;
                }

                // ── Grounded snap ──
                if (launch.grounded_ &&
                    launch.lastPlatformEntity_ != IREntity::kNullEntity) {
                    if (velocity.velocity_.z > 0.0f) {
                        velocity.velocity_.z = 0.0f;
                    }
                    if (contact.touching_ &&
                        contact.otherEntity_ != IREntity::kNullEntity) {
                        launch.lastPlatformEntity_ = contact.otherEntity_;
                    }
                    PlatformSnapshot plat =
                        fetchPlatform(launch.lastPlatformEntity_);
                    position.pos_.z =
                        plat.position_.z - launch.restOffsetZ_;
                    globalPosition.pos_ = position.pos_;
                    velocity.velocity_ = plat.velocity_;
                }

                // ── Launch check ──
                if (launch.grounded_ &&
                    launch.elapsedSeconds_ >= launch.periodSeconds_ &&
                    !launch.atMaxLaunches()) {
                    if (launch.lastPlatformEntity_ != IREntity::kNullEntity) {
                        auto springOpt =
                            IREntity::getComponentOptional<C_SpringPlatform>(
                                launch.lastPlatformEntity_);
                        if (springOpt.has_value()) {
                            springOpt.value()->launchRequested_ = true;
                        }
                    }

                    velocity.velocity_ = launch.impulseVelocity_;
                    launch.grounded_ = false;
                    launch.lastPlatformEntity_ = IREntity::kNullEntity;
                    launch.launchCount_++;
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

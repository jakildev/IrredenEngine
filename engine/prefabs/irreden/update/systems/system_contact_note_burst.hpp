#ifndef SYSTEM_CONTACT_NOTE_BURST_H
#define SYSTEM_CONTACT_NOTE_BURST_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_particle_burst.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_velocity_drag.hpp>
#include <irreden/update/components/component_lifetime.hpp>
#include <irreden/update/components/component_spawn_glow.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<CONTACT_NOTE_BURST> {
    static SystemId create() {
        return createSystem<C_ContactEvent, C_ParticleBurst, C_VoxelSetNew, C_PositionGlobal3D>(
            "ContactNoteBurst",
            [](const C_ContactEvent &contact,
               C_ParticleBurst &burst,
               C_VoxelSetNew &voxelSet,
               C_PositionGlobal3D &globalPos) {
                if (!contact.entered_) {
                    return;
                }


                const vec3 blockCenter =
                    globalPos.pos_ + vec3(voxelSet.size_) * 0.5f;
                const vec3 halfSize = vec3(voxelSet.size_) * 0.5f;
                Color color = voxelSet.voxels_[0].color_;
                float spd = burst.speed_;
                float spdXY = spd * burst.xySpeedRatio_;
                float spdZUp = spd * burst.zSpeedRatio_;
                float spdZVariance = spd * burst.zVarianceRatio_;

                const bool downward = burst.downward_;
                const float zSign = downward ? 1.0f : -1.0f;

                for (int i = 0; i < burst.count_; i++) {
                    vec3 vel;
                    if (burst.useDirectionOverride_ && burst.directionStrength_ > 0.0f) {
                        const vec3 dir =
                            IRMath::length(burst.directionOverride_) > 0.0001f
                                ? IRMath::normalize(burst.directionOverride_)
                                : vec3(0.0f, 0.0f, -1.0f);
                        const float str = burst.directionStrength_;
                        const float scatter = spd * burst.directionScatter_;
                        vec3 primary = spd * dir * str;
                        // Only override Z when direction is nearly horizontal (so we add downward)
                        // When dir.z is upward (negative), keep it for "up then fall" behavior
                        if (burst.downward_ && std::abs(dir.z) < 0.01f) {
                            primary.z = zSign * (spdZUp + randomFloat(-spdZVariance, spdZVariance));
                        }
                        vec3 randPerp = vec3(
                            randomFloat(-scatter, scatter),
                            randomFloat(-scatter, scatter),
                            randomFloat(-scatter * 0.5f, scatter * 0.5f));
                        vel = primary + randPerp * (1.0f - str);
                    } else {
                        float zVel =
                            zSign * (spdZUp + randomFloat(-spdZVariance, spdZVariance));
                        vel = vec3(
                            randomFloat(-spdXY, spdXY),
                            randomFloat(-spdXY, spdXY),
                            zVel);
                    }

                    vec3 faceOffset;
                    if (burst.useFaceSpawnBias_) {
                        const vec3 &bias = burst.faceSpawnBias_;
                        float ax = halfSize.x * (bias.x + randomFloat(-0.5f, 0.5f));
                        float ay = halfSize.y * (bias.y + randomFloat(-0.5f, 0.5f));
                        faceOffset = vec3(
                            IRMath::clamp(ax, -halfSize.x, halfSize.x),
                            IRMath::clamp(ay, -halfSize.y, halfSize.y),
                            downward ? halfSize.z : -halfSize.z);
                    } else {
                        faceOffset = vec3(
                            randomFloat(-halfSize.x, halfSize.x),
                            randomFloat(-halfSize.y, halfSize.y),
                            downward ? halfSize.z : -halfSize.z);
                    }

                    const vec3 spawnPos = isoDepthShift(
                        blockCenter + faceOffset
                            + vec3(0.0f, 0.0f, burst.spawnOffsetZ_),
                        burst.isoDepthOffset_);

                    auto applyVariance = [](float base, float variance) -> float {
                        if (variance <= 0.0f) return base;
                        return base * randomFloat(1.0f - variance, 1.0f + variance);
                    };

                    float driftDelay = applyVariance(
                        burst.pDriftDelaySeconds_, burst.hoverStartVariance_);
                    float hoverDur = applyVariance(
                        burst.pHoverDurationSec_, burst.hoverDurationVariance_);
                    float hoverAmp = applyVariance(
                        burst.pHoverOscAmplitude_, burst.hoverAmplitudeVariance_);
                    float hoverSpd = applyVariance(
                        burst.pHoverOscSpeed_, burst.hoverSpeedVariance_);

                    IREntity::EntityId entity = IREntity::createEntity(
                        C_Position3D{spawnPos},
                        C_VoxelSetNew{ivec3(1, 1, 1), color},
                        C_Velocity3D{vel},
                        C_VelocityDrag{
                            burst.pDragPerSecond_,
                            driftDelay,
                            burst.pDriftUpAccelPerSec_,
                            burst.pDragMinSpeed_,
                            burst.dragScaleX_,
                            burst.dragScaleY_,
                            burst.dragScaleZ_,
                            hoverDur,
                            hoverSpd,
                            hoverAmp,
                            burst.pHoverBlendSec_,
                            burst.pHoverBlendEasing_,
                            burst.pUsePostHoverVelocityReset_,
                            burst.pPostHoverVelocityZ_,
                            burst.pPostHoverVelocityZVariance_
                        },
                        C_Lifetime{burst.lifetime_}
                    );
                    IREntity::setComponent(entity, C_PositionGlobal3D{spawnPos});
                    if (burst.gravityEnabled_) {
                        IREntity::setComponent(entity, C_HasGravity{});
                    }
                    if (burst.glowEnabled_) {
                        IREntity::setComponent(entity, C_SpawnGlow{
                            color,
                            burst.glowColor_,
                            burst.glowHoldSeconds_,
                            burst.glowFadeSeconds_,
                            burst.glowEasing_
                        });
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CONTACT_NOTE_BURST_H */

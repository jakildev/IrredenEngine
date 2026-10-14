#ifndef SYSTEM_COLLISION_NOTE_PLATFORM_H
#define SYSTEM_COLLISION_NOTE_PLATFORM_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_overlap_contact_batch.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail {
inline bool overlaps1D(float minA, float maxA, float minB, float maxB) {
    return minA <= maxB && maxA >= minB;
}

inline void getAabbWorld(
    const C_WorldTransform &transform,
    const C_ColliderIso3DAABB &collider,
    vec3 &outMin,
    vec3 &outMax
) {
    vec3 center = transform.translation_ + collider.centerOffset_;
    outMin = center - collider.halfExtents_;
    outMax = center + collider.halfExtents_;
}

inline bool broadPhaseIsoDepth(vec3 centerA, vec3 halfA, vec3 centerB, vec3 halfB) {
    vec2 isoA = pos3DtoPos2DIso(centerA);
    vec2 isoB = pos3DtoPos2DIso(centerB);
    float isoExtentAX = halfA.x + halfA.y;
    float isoExtentBX = halfB.x + halfB.y;
    float isoExtentAY = halfA.x + halfA.y + (2.0f * halfA.z);
    float isoExtentBY = halfB.x + halfB.y + (2.0f * halfB.z);

    float depthA = static_cast<float>(pos3DtoDistance(centerA));
    float depthB = static_cast<float>(pos3DtoDistance(centerB));
    float depthExtentA = halfA.x + halfA.y + halfA.z;
    float depthExtentB = halfB.x + halfB.y + halfB.z;

    return IRMath::abs(isoA.x - isoB.x) <= (isoExtentAX + isoExtentBX) &&
           IRMath::abs(isoA.y - isoB.y) <= (isoExtentAY + isoExtentBY) &&
           IRMath::abs(depthA - depthB) <= (depthExtentA + depthExtentB);
}

inline bool broadPhaseIsoDepth(
    const C_WorldTransform &transformA,
    const C_ColliderIso3DAABB &a,
    const C_WorldTransform &transformB,
    const C_ColliderIso3DAABB &b
) {
    return broadPhaseIsoDepth(
        transformA.translation_ + a.centerOffset_,
        a.halfExtents_,
        transformB.translation_ + b.centerOffset_,
        b.halfExtents_
    );
}

// Swept broad phase: A's box stretched back over `motion`, its displacement
// relative to B this frame, so a pair that crossed between frames survives.
inline bool broadPhaseIsoDepthSwept(
    const C_WorldTransform &transformA,
    const C_ColliderIso3DAABB &a,
    const C_WorldTransform &transformB,
    const C_ColliderIso3DAABB &b,
    vec3 motion
) {
    return broadPhaseIsoDepth(
        transformA.translation_ + a.centerOffset_ - motion * 0.5f,
        a.halfExtents_ + IRMath::abs(motion) * 0.5f,
        transformB.translation_ + b.centerOffset_,
        b.halfExtents_
    );
}

inline bool narrowPhaseAabb(
    const C_WorldTransform &transformA,
    const C_ColliderIso3DAABB &a,
    const C_WorldTransform &transformB,
    const C_ColliderIso3DAABB &b,
    vec3 &outMinA,
    vec3 &outMaxA,
    vec3 &outMinB,
    vec3 &outMaxB
) {
    getAabbWorld(transformA, a, outMinA, outMaxA);
    getAabbWorld(transformB, b, outMinB, outMaxB);

    return overlaps1D(outMinA.x, outMaxA.x, outMinB.x, outMaxB.x) &&
           overlaps1D(outMinA.y, outMaxA.y, outMinB.y, outMaxB.y) &&
           overlaps1D(outMinA.z, outMaxA.z, outMinB.z, outMaxB.z);
}

// `narrowPhaseAabb` over the frame: A moved by `motion` relative to B.
inline bool narrowPhaseAabbSwept(
    const C_WorldTransform &transformA,
    const C_ColliderIso3DAABB &a,
    const C_WorldTransform &transformB,
    const C_ColliderIso3DAABB &b,
    vec3 motion,
    vec3 &outMinA,
    vec3 &outMaxA,
    vec3 &outMinB,
    vec3 &outMaxB
) {
    getAabbWorld(transformA, a, outMinA, outMaxA);
    getAabbWorld(transformB, b, outMinB, outMaxB);
    return sweptAabbOverlap(outMinA, outMaxA, outMinB, outMaxB, motion);
}

// The node's C_Velocity3D column, or nullptr when its entities have none
// (a static collider: no motion this frame).
inline const std::vector<C_Velocity3D> *
velocityColumn(IREntity::ArchetypeNode *node, IREntity::ComponentId velocityType) {
    if (!node->type_.contains(velocityType)) {
        return nullptr;
    }
    return &IREntity::getComponentData<C_Velocity3D>(node);
}

inline void markContact(C_ContactEvent &event, IREntity::EntityId otherEntity, vec3 normal) {
    event.touching_ = true;
    event.exited_ = false;
    event.otherEntity_ = otherEntity;
    event.normal_ = normal;
    if (event.wasTouching_) {
        event.stayed_ = true;
    } else {
        event.entered_ = true;
    }
}
} // namespace detail

// Contacts are swept over the frame: a collider with a C_Velocity3D moved by
// velocity * dt since the previous tick (VELOCITY_3D integrates exactly that
// ahead of this system), and a pair counts as touching if the two boxes met
// at any moment in between. A fast body therefore never passes through a thin
// one between two frames. Colliders without a velocity are static here, and a
// position written directly (a snap, a teleport) is not swept. The velocity is
// read as world motion, which holds for unparented colliders.
template <> struct System<COLLISION_NOTE_PLATFORM> {
    static SystemId create() {
        return createSystem<
            C_WorldTransform,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent>(
            "CollisionNotePlatform",
            [](const Archetype &selfArchetype,
               std::vector<IREntity::EntityId> &entities,
               std::vector<C_WorldTransform> &transforms,
               std::vector<C_ColliderIso3DAABB> &colliders,
               std::vector<C_CollisionLayer> &layers,
               std::vector<C_ContactEvent> &events) {
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

                // Opt-in batched overlap-pair emission. The singleton
                // exists only when a creation registered
                // DISPATCH_LUA_OVERLAP, so existing C_ContactEvent-only
                // users keep the fast first-contact break path (nullptr).
                auto *overlapBatch = IREntity::singletonOrNull<C_OverlapContactBatch>();
                const bool emitPairs = overlapBatch != nullptr;

                auto allColliderNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<
                        C_WorldTransform,
                        C_ColliderIso3DAABB,
                        C_CollisionLayer,
                        C_ContactEvent>()
                );

                const IREntity::ComponentId velocityType =
                    IREntity::getComponentType<C_Velocity3D>();
                const std::vector<C_Velocity3D> *selfVelocities = nullptr;
                for (auto *node : allColliderNodes) {
                    if (node->type_ == selfArchetype) {
                        selfVelocities = detail::velocityColumn(node, velocityType);
                        break;
                    }
                }
                // Read only when some collider moves: a scene of static
                // colliders (and a test without a TimeManager) never asks.
                float dt = -1.0f;
                auto frameDt = [&dt]() {
                    if (dt < 0.0f) {
                        dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
                    }
                    return dt;
                };

                for (int i = 0; i < static_cast<int>(entities.size()); i++) {
                    IR_PROFILE_BLOCK("CollisionBlockScan", IR_PROFILER_COLOR_UPDATE);
                    auto &selfEntity = entities[i];
                    auto &selfTransform = transforms[i];
                    auto &selfCollider = colliders[i];
                    auto &selfLayer = layers[i];
                    auto &selfEvent = events[i];

                    bool foundAnyContact = false;
                    const vec3 selfVelocity =
                        selfVelocities != nullptr ? (*selfVelocities)[i].velocity_ : vec3(0.0f);

                    vec3 blockMin(0.0f);
                    vec3 blockMax(0.0f);
                    vec3 platformMin(0.0f);
                    vec3 platformMax(0.0f);

                    for (auto *otherNode : allColliderNodes) {
                        auto &otherEntities = otherNode->entities_;
                        auto &otherTransforms =
                            IREntity::getComponentData<C_WorldTransform>(otherNode);
                        auto &otherColliders =
                            IREntity::getComponentData<C_ColliderIso3DAABB>(otherNode);
                        auto &otherLayers = IREntity::getComponentData<C_CollisionLayer>(otherNode);
                        auto &otherEvents = IREntity::getComponentData<C_ContactEvent>(otherNode);
                        const std::vector<C_Velocity3D> *otherVelocities =
                            detail::velocityColumn(otherNode, velocityType);

                        for (int j = 0; j < otherNode->length_; j++) {
                            if (otherEntities[j] == selfEntity) {
                                continue;
                            }
                            auto &otherLayer = otherLayers[j];
                            if (!selfLayer.canCollideWith(otherLayer) ||
                                !otherLayer.canCollideWith(selfLayer)) {
                                continue;
                            }

                            auto &otherTransform = otherTransforms[j];
                            auto &otherCollider = otherColliders[j];

                            const vec3 relativeVelocity =
                                selfVelocity - (otherVelocities != nullptr
                                                    ? (*otherVelocities)[j].velocity_
                                                    : vec3(0.0f));
                            const bool moving = relativeVelocity != vec3(0.0f);
                            const vec3 motion = moving ? relativeVelocity * frameDt() : vec3(0.0f);

                            if (!detail::broadPhaseIsoDepthSwept(
                                    selfTransform,
                                    selfCollider,
                                    otherTransform,
                                    otherCollider,
                                    motion
                                )) {
                                continue;
                            }

                            if (!detail::narrowPhaseAabbSwept(
                                    selfTransform,
                                    selfCollider,
                                    otherTransform,
                                    otherCollider,
                                    motion,
                                    blockMin,
                                    blockMax,
                                    platformMin,
                                    platformMax
                                )) {
                                continue;
                            }

                            // C_ContactEvent keeps its single-contact-per-entity
                            // semantics: mark only the first overlap, so existing
                            // consumers (spring/midi/glow) are byte-identical on
                            // both the emit and non-emit paths.
                            if (!foundAnyContact) {
                                foundAnyContact = true;
                                detail::markContact(
                                    selfEvent,
                                    otherEntities[j],
                                    vec3(0.0f, 0.0f, 1.0f)
                                );
                                detail::markContact(
                                    otherEvents[j],
                                    selfEntity,
                                    vec3(0.0f, 0.0f, -1.0f)
                                );
                            }

                            if (emitPairs) {
                                // Emit each pair once (canonical self < other);
                                // both layers are stamped so the dispatcher never
                                // reaches the foreign entity. Keep scanning —
                                // pair-level enter/exit needs ALL overlaps, not
                                // just the first. The vector is cleared (not
                                // freed) by the dispatcher each frame, so after
                                // warmup this push_back reuses capacity and never
                                // reallocates.
                                if (selfEntity < otherEntities[j]) {
                                    overlapBatch->pairs_.push_back(
                                        ContactPair{
                                            selfEntity,
                                            otherEntities[j],
                                            selfLayer.layer_,
                                            otherLayer.layer_,
                                            vec3(0.0f, 0.0f, 1.0f)
                                        }
                                    );
                                }
                                continue;
                            }
                            break;
                        }
                        if (!emitPairs && foundAnyContact) {
                            break;
                        }
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COLLISION_NOTE_PLATFORM_H */

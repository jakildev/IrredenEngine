#ifndef SYSTEM_COLLISION_NOTE_PLATFORM_H
#define SYSTEM_COLLISION_NOTE_PLATFORM_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_collision_pair_buffer.hpp>
#include <irreden/update/components/component_contact_event.hpp>

#include <vector>

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

inline bool broadPhaseIsoDepth(
    const C_WorldTransform &transformA,
    const C_ColliderIso3DAABB &a,
    const C_WorldTransform &transformB,
    const C_ColliderIso3DAABB &b
) {
    vec3 centerA = transformA.translation_ + a.centerOffset_;
    vec3 centerB = transformB.translation_ + b.centerOffset_;

    vec2 isoA = pos3DtoPos2DIso(centerA);
    vec2 isoB = pos3DtoPos2DIso(centerB);
    float isoExtentAX = a.halfExtents_.x + a.halfExtents_.y;
    float isoExtentBX = b.halfExtents_.x + b.halfExtents_.y;
    float isoExtentAY = a.halfExtents_.x + a.halfExtents_.y + (2.0f * a.halfExtents_.z);
    float isoExtentBY = b.halfExtents_.x + b.halfExtents_.y + (2.0f * b.halfExtents_.z);

    float depthA = static_cast<float>(pos3DtoDistance(centerA));
    float depthB = static_cast<float>(pos3DtoDistance(centerB));
    float depthExtentA = a.halfExtents_.x + a.halfExtents_.y + a.halfExtents_.z;
    float depthExtentB = b.halfExtents_.x + b.halfExtents_.y + b.halfExtents_.z;

    return IRMath::abs(isoA.x - isoB.x) <= (isoExtentAX + isoExtentBX) &&
           IRMath::abs(isoA.y - isoB.y) <= (isoExtentAY + isoExtentBY) &&
           IRMath::abs(depthA - depthB) <= (depthExtentA + depthExtentB);
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

// Broad+narrow AABB overlap detection over the iso-3D collider archetype.
// Two additive outputs per frame:
//   1. Per-entity C_ContactEvent (first overlapping partner wins) — the
//      original behavior, consumed by system_spring_platform /
//      system_contact_* / the music demos. Unchanged.
//   2. A batched, layer-stamped ContactPair vector in the
//      C_CollisionPairBuffer singleton — every confirmed overlap, emitted
//      once per unordered pair. Consumed by COLLISION_DISPATCH_LUA_OVERLAP
//      to drive Lua overlap callbacks (#1817).
//
// Migrated to the registerSystem member form (`.claude/rules/cpp-systems.md`)
// so beginTick can clear the singleton buffer once per frame before the
// scan refills it — the buffer's whole lifecycle lives on the producer, so a
// pipeline that runs this system without the dispatch consumer still clears
// every frame (no unbounded growth).
template <> struct System<COLLISION_NOTE_PLATFORM> {
    C_CollisionPairBuffer *pairBuffer_ = nullptr;

    void beginTick() {
        pairBuffer_ = &IREntity::singleton<C_CollisionPairBuffer>();
        // Clear-not-release: capacity persists across frames, so the steady
        // state appends without reallocating.
        pairBuffer_->pairs_.clear();
    }

    void tick(
        const Archetype &,
        std::vector<IREntity::EntityId> &entities,
        std::vector<C_WorldTransform> &transforms,
        std::vector<C_ColliderIso3DAABB> &colliders,
        std::vector<C_CollisionLayer> &layers,
        std::vector<C_ContactEvent> &events
    ) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

        auto allColliderNodes = IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<
                C_WorldTransform,
                C_ColliderIso3DAABB,
                C_CollisionLayer,
                C_ContactEvent>()
        );

        for (int i = 0; i < static_cast<int>(entities.size()); i++) {
            IR_PROFILE_BLOCK("CollisionBlockScan", IR_PROFILER_COLOR_UPDATE);
            auto &selfEntity = entities[i];
            auto &selfTransform = transforms[i];
            auto &selfCollider = colliders[i];
            auto &selfLayer = layers[i];
            auto &selfEvent = events[i];

            bool foundAnyContact = false;

            vec3 blockMin(0.0f);
            vec3 blockMax(0.0f);
            vec3 platformMin(0.0f);
            vec3 platformMax(0.0f);

            for (auto *otherNode : allColliderNodes) {
                auto &otherEntities = otherNode->entities_;
                auto &otherTransforms = IREntity::getComponentData<C_WorldTransform>(otherNode);
                auto &otherColliders = IREntity::getComponentData<C_ColliderIso3DAABB>(otherNode);
                auto &otherLayers = IREntity::getComponentData<C_CollisionLayer>(otherNode);
                auto &otherEvents = IREntity::getComponentData<C_ContactEvent>(otherNode);

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

                    if (!detail::broadPhaseIsoDepth(
                            selfTransform,
                            selfCollider,
                            otherTransform,
                            otherCollider
                        )) {
                        continue;
                    }

                    if (!detail::narrowPhaseAabb(
                            selfTransform,
                            selfCollider,
                            otherTransform,
                            otherCollider,
                            blockMin,
                            blockMax,
                            platformMin,
                            platformMax
                        )) {
                        continue;
                    }

                    // Emit each unordered overlap exactly once: only the
                    // smaller-id "self" side records the pair (the larger-id
                    // side sees the same overlap and skips). layerA_/layerB_
                    // are stamped here from the layers already in hand, so the
                    // dispatch consumer needs zero foreign getComponent.
                    if (selfEntity < otherEntities[j]) {
                        pairBuffer_->pairs_.push_back(
                            ContactPair{
                                selfEntity,
                                otherEntities[j],
                                selfLayer.layer_,
                                otherLayer.layer_
                            }
                        );
                    }

                    // C_ContactEvent keeps its first-contact-wins semantics:
                    // only the first overlapping partner is stamped onto each
                    // entity's event (identical to the pre-#1817 behavior).
                    if (!foundAnyContact) {
                        foundAnyContact = true;
                        detail::markContact(selfEvent, otherEntities[j], vec3(0.0f, 0.0f, 1.0f));
                        detail::markContact(otherEvents[j], selfEntity, vec3(0.0f, 0.0f, -1.0f));
                    }

                    // No break: keep scanning so every overlapping pair is
                    // emitted into the buffer, even when one entity touches
                    // several others in the same frame.
                }
            }
        }
    }

    static SystemId create() {
        // Ensure the shared pair buffer singleton exists before any tick so
        // COLLISION_DISPATCH_LUA_OVERLAP's archetype matcher finds it on the
        // first frame (getOrCreateSingleton is idempotent).
        IREntity::singletonEntity<C_CollisionPairBuffer>();
        return registerSystem<
            COLLISION_NOTE_PLATFORM,
            C_WorldTransform,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent>("CollisionNotePlatform");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COLLISION_NOTE_PLATFORM_H */

#ifndef SYSTEM_COLLISION_NOTE_PLATFORM_H
#define SYSTEM_COLLISION_NOTE_PLATFORM_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_contact_event.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace {
inline bool overlaps1D(float minA, float maxA, float minB, float maxB) {
    return minA <= maxB && maxA >= minB;
}

inline void getAabbWorld(
    const C_PositionGlobal3D &position,
    const C_ColliderIso3DAABB &collider,
    vec3 &outMin,
    vec3 &outMax
) {
    vec3 center = position.pos_ + collider.centerOffset_;
    outMin = center - collider.halfExtents_;
    outMax = center + collider.halfExtents_;
}

inline bool broadPhaseIsoDepth(
    const C_PositionGlobal3D &positionA,
    const C_ColliderIso3DAABB &a,
    const C_PositionGlobal3D &positionB,
    const C_ColliderIso3DAABB &b
) {
    vec3 centerA = positionA.pos_ + a.centerOffset_;
    vec3 centerB = positionB.pos_ + b.centerOffset_;

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
    const C_PositionGlobal3D &positionA,
    const C_ColliderIso3DAABB &a,
    const C_PositionGlobal3D &positionB,
    const C_ColliderIso3DAABB &b,
    vec3 &outMinA,
    vec3 &outMaxA,
    vec3 &outMinB,
    vec3 &outMaxB
) {
    getAabbWorld(positionA, a, outMinA, outMaxA);
    getAabbWorld(positionB, b, outMinB, outMaxB);

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
} // namespace

template <> struct System<COLLISION_NOTE_PLATFORM> {
    static SystemId create() {
        return createSystem<
            C_PositionGlobal3D,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent>(
            "CollisionNotePlatform",
            [](const Archetype &,
               std::vector<IREntity::EntityId> &entities,
               std::vector<C_PositionGlobal3D> &positions,
               std::vector<C_ColliderIso3DAABB> &colliders,
               std::vector<C_CollisionLayer> &layers,
               std::vector<C_ContactEvent> &events) {
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

                auto allColliderNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<
                        C_PositionGlobal3D,
                        C_ColliderIso3DAABB,
                        C_CollisionLayer,
                        C_ContactEvent>()
                );

                for (int i = 0; i < static_cast<int>(entities.size()); i++) {
                    IR_PROFILE_BLOCK("CollisionBlockScan", IR_PROFILER_COLOR_UPDATE);
                    auto &selfEntity = entities[i];
                    auto &selfPos = positions[i];
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
                        auto &otherPositions =
                            IREntity::getComponentData<C_PositionGlobal3D>(otherNode);
                        auto &otherColliders =
                            IREntity::getComponentData<C_ColliderIso3DAABB>(otherNode);
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

                            auto &otherPos = otherPositions[j];
                            auto &otherCollider = otherColliders[j];

                            if (!broadPhaseIsoDepth(
                                    selfPos,
                                    selfCollider,
                                    otherPos,
                                    otherCollider
                                )) {
                                continue;
                            }

                            if (!narrowPhaseAabb(
                                    selfPos,
                                    selfCollider,
                                    otherPos,
                                    otherCollider,
                                    blockMin,
                                    blockMax,
                                    platformMin,
                                    platformMax
                                )) {
                                continue;
                            }

                            foundAnyContact = true;
                            markContact(selfEvent, otherEntities[j], vec3(0.0f, 0.0f, 1.0f));
                            markContact(otherEvents[j], selfEntity, vec3(0.0f, 0.0f, -1.0f));
                            break;
                        }
                        if (foundAnyContact) {
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

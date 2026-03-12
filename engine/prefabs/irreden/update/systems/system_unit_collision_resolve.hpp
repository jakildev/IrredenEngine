#ifndef SYSTEM_UNIT_COLLISION_RESOLVE_H
#define SYSTEM_UNIT_COLLISION_RESOLVE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/update/nav_query.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_collider_circle.hpp>
#include <irreden/update/components/component_flow_field_agent.hpp>
#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail_collision {

struct SpatialEntry {
    IREntity::EntityId entity;
    float x;
    float y;
    float radius;
    bool moving;
};

inline int64_t spatialKey(int cx, int cy) {
    return (static_cast<int64_t>(cx) << 32) | (static_cast<int64_t>(cy) & 0xFFFFFFFF);
}

static std::unordered_map<int64_t, std::vector<SpatialEntry>> s_hash;
static float s_cellSize = 20.0f;
static const C_NavWorld *s_navWorld = nullptr;
static const C_ChunkRegistry *s_registry = nullptr;

} // namespace detail_collision

template <> struct System<UNIT_COLLISION_RESOLVE> {
    static SystemId create() {
        return createSystem<C_Position3D, C_ColliderCircle, C_NavAgent>(
            "UnitCollisionResolve",
            [](IREntity::EntityId entity,
               C_Position3D &pos,
               C_ColliderCircle &collider,
               C_NavAgent &agent) {
                auto flowAgent = IREntity::getComponentOptional<C_FlowFieldAgent>(entity);
                if (agent.hasPath() || (flowAgent && (*flowAgent)->hasField())) return;

                float dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
                float cx = pos.pos_.x + collider.centerOffset_.x;
                float cy = pos.pos_.y + collider.centerOffset_.y;
                int cellX = static_cast<int>(std::floor(cx / detail_collision::s_cellSize));
                int cellY = static_cast<int>(std::floor(cy / detail_collision::s_cellSize));

                constexpr float kSeparationSpeed = 8.0f;
                float maxPush = kSeparationSpeed * dt;

                float pushX = 0.0f;
                float pushY = 0.0f;

                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        int64_t key = detail_collision::spatialKey(cellX + dx, cellY + dy);
                        auto it = detail_collision::s_hash.find(key);
                        if (it == detail_collision::s_hash.end()) continue;

                        for (const auto &other : it->second) {
                            if (other.entity == entity) continue;
                            if (other.moving) continue;

                            float diffX = cx - other.x;
                            float diffY = cy - other.y;
                            float distSq = diffX * diffX + diffY * diffY;
                            float minDist =
                                collider.movementCollisionRadius_ + other.radius;
                            float minDistSq = minDist * minDist;

                            if (distSq < minDistSq) {
                                float overlap = 0.0f;
                                float nx = 0.0f;
                                float ny = 0.0f;

                                if (distSq <= 0.0001f) {
                                    uint64_t seed =
                                        static_cast<uint64_t>(entity) * 73856093ull ^
                                        static_cast<uint64_t>(other.entity) * 19349663ull;
                                    float angle = static_cast<float>(seed % 360ull) *
                                                  (3.14159265f / 180.0f);
                                    nx = std::cos(angle);
                                    ny = std::sin(angle);
                                    overlap = minDist;
                                } else {
                                    float dist = std::sqrt(distSq);
                                    overlap = minDist - dist;
                                    nx = diffX / dist;
                                    ny = diffY / dist;
                                }

                                float push = std::min(overlap * 0.3f, maxPush);
                                pushX += nx * push;
                                pushY += ny * push;
                            }
                        }
                    }
                }

                float pushLen = std::sqrt(pushX * pushX + pushY * pushY);
                if (pushLen < 0.0001f) return;

                if (pushLen > maxPush) {
                    float scale = maxPush / pushLen;
                    pushX *= scale;
                    pushY *= scale;
                }

                // Don't push into walls
                if (detail_collision::s_navWorld) {
                    float newCX = cx + pushX;
                    float newCY = cy + pushY;
                    if (navCircleOverlapsWall(
                            *detail_collision::s_navWorld,
                            *detail_collision::s_registry,
                            newCX, newCY, 0.0f, collider.movementCollisionRadius_)) {
                        return;
                    }
                }

                pos.pos_.x += pushX;
                pos.pos_.y += pushY;
            },
            []() {
                detail_collision::s_hash.clear();

                auto navNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_NavWorld, C_ChunkRegistry>()
                );
                if (!navNodes.empty()) {
                    auto id = navNodes[0]->entities_[0];
                    detail_collision::s_navWorld =
                        &IREntity::getComponent<C_NavWorld>(id);
                    detail_collision::s_registry =
                        &IREntity::getComponent<C_ChunkRegistry>(id);
                } else {
                    detail_collision::s_navWorld = nullptr;
                    detail_collision::s_registry = nullptr;
                }

                auto nodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_Position3D, C_ColliderCircle, C_NavAgent>()
                );

                float maxRadius = 0.0f;
                for (auto *node : nodes) {
                    auto &colliders = IREntity::getComponentData<C_ColliderCircle>(node);
                    for (size_t i = 0; i < colliders.size(); i++) {
                        float r = colliders[i].movementCollisionRadius_;
                        if (r > maxRadius) maxRadius = r;
                    }
                }
                detail_collision::s_cellSize = std::max(1.0f, maxRadius * 4.0f);

                for (auto *node : nodes) {
                    auto &positions = IREntity::getComponentData<C_Position3D>(node);
                    auto &colliders = IREntity::getComponentData<C_ColliderCircle>(node);
                    auto &agents = IREntity::getComponentData<C_NavAgent>(node);
                    for (size_t i = 0; i < positions.size(); i++) {
                        float px = positions[i].pos_.x + colliders[i].centerOffset_.x;
                        float py = positions[i].pos_.y + colliders[i].centerOffset_.y;
                        int cx = static_cast<int>(std::floor(px / detail_collision::s_cellSize));
                        int cy = static_cast<int>(std::floor(py / detail_collision::s_cellSize));
                        int64_t key = detail_collision::spatialKey(cx, cy);
                        bool isFlowMoving = false;
                        if (auto flowAgent = IREntity::getComponentOptional<C_FlowFieldAgent>(
                                node->entities_[i])) {
                            isFlowMoving = (*flowAgent)->hasField();
                        }
                        detail_collision::s_hash[key].push_back({
                            node->entities_[i],
                            px,
                            py,
                            colliders[i].movementCollisionRadius_,
                            agents[i].hasPath() || isFlowMoving
                        });
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_UNIT_COLLISION_RESOLVE_H */

#ifndef SYSTEM_SMOOTH_MOVEMENT_H
#define SYSTEM_SMOOTH_MOVEMENT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/update/nav_query.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_smooth_movement.hpp>
#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/update/components/component_move_order.hpp>
#include <irreden/update/components/component_collider_circle.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>

#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail_smooth {
    static IREntity::EntityId s_levelEntity = IREntity::kNullEntity;
    static const C_NavWorld *s_navWorld = nullptr;
    static const C_ChunkRegistry *s_registry = nullptr;

    struct UnitEntry {
        IREntity::EntityId entity;
        float x, y;
        float radius;
    };

    static std::unordered_map<int64_t, std::vector<UnitEntry>> s_unitHash;
    static float s_unitCellSize = 20.0f;

    inline int64_t unitKey(int cx, int cy) {
        return (static_cast<int64_t>(cx) << 32) | (static_cast<int64_t>(cy) & 0xFFFFFFFF);
    }

    inline bool wouldOverlapUnit(
        IREntity::EntityId self, float x, float y, float radius
    ) {
        int cellX = static_cast<int>(std::floor(x / s_unitCellSize));
        int cellY = static_cast<int>(std::floor(y / s_unitCellSize));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int64_t key = unitKey(cellX + dx, cellY + dy);
                auto it = s_unitHash.find(key);
                if (it == s_unitHash.end()) continue;
                for (const auto &other : it->second) {
                    if (other.entity == self) continue;
                    float diffX = x - other.x;
                    float diffY = y - other.y;
                    float distSq = diffX * diffX + diffY * diffY;
                    float minDist = radius + other.radius;
                    if (distSq < minDist * minDist) return true;
                }
            }
        }
        return false;
    }
}

template <> struct System<SMOOTH_MOVEMENT> {
    static SystemId create() {
        return createSystem<
            C_Position3D,
            C_SmoothMovement,
            C_NavAgent,
            C_ColliderCircle>(
            "SmoothMovement",
            [](IREntity::EntityId entity,
               C_Position3D &position,
               const C_SmoothMovement &,
               C_NavAgent &agent,
               const C_ColliderCircle &collider) {
                if (!detail_smooth::s_navWorld) return;

                const auto &nw = *detail_smooth::s_navWorld;
                const auto &reg = *detail_smooth::s_registry;
                float cellSize = nw.cellSizeWorld_;
                float dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
                vec3 current = position.pos_;

                // Push out of wall
                float curCX = current.x + collider.centerOffset_.x;
                float curCY = current.y + collider.centerOffset_.y;
                if (navCircleOverlapsWall(nw, reg, curCX, curCY, 0.0f, collider.radius_)) {
                    constexpr int kEscapeRays = 16;
                    float escapeDist = cellSize * 0.5f;
                    for (int attempt = 0; attempt < 4; attempt++) {
                        for (int i = 0; i < kEscapeRays; i++) {
                            float angle = static_cast<float>(i) * (2.0f * 3.14159265f / kEscapeRays);
                            vec3 tryPos = current + vec3(
                                std::cos(angle) * escapeDist,
                                std::sin(angle) * escapeDist,
                                0.0f);
                            float tryX = tryPos.x + collider.centerOffset_.x;
                            float tryY = tryPos.y + collider.centerOffset_.y;
                            if (!navCircleOverlapsWall(nw, reg, tryX, tryY, 0.0f, collider.radius_)) {
                                position.pos_ = tryPos;
                                current = tryPos;
                                goto escaped;
                            }
                        }
                        escapeDist += cellSize * 0.5f;
                    }
                    escaped:;
                }

                if (!agent.hasPath()) return;

                constexpr int kMaxStuckFrames = 45;

                ivec3 targetCellPos = agent.path_[static_cast<size_t>(agent.pathIndex_)];
                vec3 targetWorld = navCellToWorld(nw, reg, targetCellPos);
                targetWorld.z = current.z;

                vec3 delta = targetWorld - current;
                float dist = IRMath::length(delta);
                float moveDist = agent.moveSpeed_ * dt;

                // Waypoint arrival
                float arriveRadius = cellSize * 0.4f;
                if (dist < arriveRadius) {
                    agent.pathIndex_++;
                    agent.stuckFrames_ = 0;
                    if (agent.pathIndex_ >= static_cast<int>(agent.path_.size())) {
                        if (agent.partialPath_) {
                            IREntity::setComponentDeferred(entity, C_MoveOrder(agent.finalTarget_));
                        }
                        agent.clearPath();
                    }
                    return;
                }

                vec3 dir = (dist > 0.001f) ? delta / dist : vec3(0.0f);

                // Find best movement position considering walls and wall clearance.
                vec3 candidatePos = current;
                vec3 bestPos = current;
                float bestScore = std::numeric_limits<float>::infinity();
                bool foundCandidate = false;
                float preferredWallClearance = collider.radius_ + cellSize * 0.5f;

                auto tryCandidate = [&](vec3 tryPos) {
                    float fromX = current.x + collider.centerOffset_.x;
                    float fromY = current.y + collider.centerOffset_.y;
                    float tryX = tryPos.x + collider.centerOffset_.x;
                    float tryY = tryPos.y + collider.centerOffset_.y;
                    if (navCirclePathOverlapsWall(
                            nw,
                            reg,
                            fromX,
                            fromY,
                            tryX,
                            tryY,
                            0.0f,
                            collider.radius_)) {
                        return;
                    }

                    float wallClearance = navGetClearanceAtWorld(nw, reg, tryX, tryY, 0.0f);
                    float wallPenalty = 0.0f;
                    if (wallClearance < preferredWallClearance) {
                        wallPenalty =
                            (preferredWallClearance - wallClearance) * cellSize * 3.0f;
                    }

                    float d = IRMath::length(tryPos - targetWorld);
                    float score = d + wallPenalty;
                    if (!foundCandidate || score < bestScore) {
                        bestPos = tryPos;
                        bestScore = score;
                        foundCandidate = true;
                    }
                };

                tryCandidate(current + dir * moveDist);
                tryCandidate(current + vec3(dir.x * moveDist, 0.0f, 0.0f));
                tryCandidate(current + vec3(0.0f, dir.y * moveDist, 0.0f));

                constexpr int kNumRays = 8;
                for (int i = 0; i < kNumRays; i++) {
                    float angle = static_cast<float>(i) * (2.0f * 3.14159265f / kNumRays);
                    tryCandidate(current + vec3(
                        std::cos(angle) * moveDist,
                        std::sin(angle) * moveDist,
                        0.0f));
                }

                if (foundCandidate) {
                    candidatePos = bestPos;
                }

                // Unit-unit avoidance
                float candCX = candidatePos.x + collider.centerOffset_.x;
                float candCY = candidatePos.y + collider.centerOffset_.y;
                if (detail_smooth::wouldOverlapUnit(
                        entity,
                        candCX,
                        candCY,
                        collider.movementCollisionRadius_)) {
                    bool isFinal = (agent.pathIndex_ == static_cast<int>(agent.path_.size()) - 1);
                    if (isFinal && dist < collider.movementCollisionRadius_ * 4.0f) {
                        agent.clearPath();
                        return;
                    }
                    agent.stuckFrames_++;
                    if (agent.stuckFrames_ > kMaxStuckFrames) {
                        agent.clearPath();
                    }
                    return;
                }

                // Check if we actually made progress
                float moveMag = IRMath::length(candidatePos - current);
                if (moveMag < moveDist * 0.1f) {
                    agent.stuckFrames_++;
                    if (agent.stuckFrames_ > kMaxStuckFrames) {
                        agent.clearPath();
                        return;
                    }
                } else {
                    agent.stuckFrames_ = 0;
                }

                position.pos_ = candidatePos;
            },
            []() {
                auto nodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_NavWorld, C_ChunkRegistry>()
                );
                if (!nodes.empty()) {
                    detail_smooth::s_levelEntity = nodes[0]->entities_[0];
                    detail_smooth::s_navWorld =
                        &IREntity::getComponent<C_NavWorld>(detail_smooth::s_levelEntity);
                    detail_smooth::s_registry =
                        &IREntity::getComponent<C_ChunkRegistry>(detail_smooth::s_levelEntity);
                } else {
                    detail_smooth::s_levelEntity = IREntity::kNullEntity;
                    detail_smooth::s_navWorld = nullptr;
                    detail_smooth::s_registry = nullptr;
                }

                // Build spatial hash of all units for unit-unit avoidance
                detail_smooth::s_unitHash.clear();
                auto unitNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_Position3D, C_ColliderCircle>()
                );
                float maxR = 0.0f;
                for (auto *node : unitNodes) {
                    auto &colliders = IREntity::getComponentData<C_ColliderCircle>(node);
                    for (size_t i = 0; i < colliders.size(); i++) {
                        if (colliders[i].movementCollisionRadius_ > maxR) {
                            maxR = colliders[i].movementCollisionRadius_;
                        }
                    }
                }
                detail_smooth::s_unitCellSize = std::max(1.0f, maxR * 4.0f);

                for (auto *node : unitNodes) {
                    auto &positions = IREntity::getComponentData<C_Position3D>(node);
                    auto &colliders = IREntity::getComponentData<C_ColliderCircle>(node);
                    for (size_t i = 0; i < positions.size(); i++) {
                        float px = positions[i].pos_.x + colliders[i].centerOffset_.x;
                        float py = positions[i].pos_.y + colliders[i].centerOffset_.y;
                        int cx = static_cast<int>(std::floor(px / detail_smooth::s_unitCellSize));
                        int cy = static_cast<int>(std::floor(py / detail_smooth::s_unitCellSize));
                        int64_t key = detail_smooth::unitKey(cx, cy);
                        detail_smooth::s_unitHash[key].push_back({
                            node->entities_[i], px, py, colliders[i].movementCollisionRadius_
                        });
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SMOOTH_MOVEMENT_H */

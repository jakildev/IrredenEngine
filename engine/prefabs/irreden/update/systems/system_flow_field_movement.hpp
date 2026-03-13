#ifndef SYSTEM_FLOW_FIELD_MOVEMENT_H
#define SYSTEM_FLOW_FIELD_MOVEMENT_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_smooth_movement.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>
#include <irreden/update/components/component_collider_circle.hpp>
#include <irreden/update/components/component_flow_field.hpp>
#include <irreden/update/components/component_flow_field_agent.hpp>
#include <irreden/update/components/component_move_order.hpp>
#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/nav_query.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail_flow_move {

struct UnitEntry {
    IREntity::EntityId entity_{IREntity::kNullEntity};
    float x{0.0f};
    float y{0.0f};
    float hardRadius{0.0f};
    float preferredRadius{0.0f};
    bool moving{false};
};

static const C_NavWorld *s_navWorld = nullptr;
static const C_ChunkRegistry *s_registry = nullptr;
static const C_FlowField *s_flowField = nullptr;
static std::unordered_map<int64_t, std::vector<UnitEntry>> s_unitHash;
static float s_unitCellSize = 20.0f;
static constexpr bool kSoftCollisionForMovingUnits = false;
static constexpr float kMovingUnitPenaltyWeight = 10.0f;
static constexpr float kCrowdLookAheadWeight = 0.7f;

inline int64_t unitKey(int cx, int cy) {
    return (static_cast<int64_t>(cx) << 32) | (static_cast<int64_t>(cy) & 0xFFFFFFFF);
}

inline vec3 buildImmediateDirection(vec3 from, vec3 to) {
    vec3 delta = to - from;
    delta.z = 0.0f;
    float len = IRMath::length(delta);
    if (len < 0.001f) return vec3(0.0f);
    return delta / len;
}

struct UnitSpacingSample {
    bool hardBlocked{false};
    bool blockedByMoving{false};
    bool blockedByIdle{false};
    float softPenalty{0.0f};
};

inline UnitSpacingSample sampleUnitSpacing(
    IREntity::EntityId self,
    float x,
    float y,
    float hardRadius,
    float preferredRadius
) {
    UnitSpacingSample sample;
    int cellX = static_cast<int>(std::floor(x / s_unitCellSize));
    int cellY = static_cast<int>(std::floor(y / s_unitCellSize));
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            auto it = s_unitHash.find(unitKey(cellX + dx, cellY + dy));
            if (it == s_unitHash.end()) continue;
            for (const auto &other : it->second) {
                if (other.entity_ == self) continue;

                float diffX = x - other.x;
                float diffY = y - other.y;
                float distSq = diffX * diffX + diffY * diffY;

                float hardMinDist = hardRadius + other.hardRadius;
                if (distSq < hardMinDist * hardMinDist) {
                    if (kSoftCollisionForMovingUnits && other.moving) {
                        float dist = std::sqrt(std::max(distSq, 0.0001f));
                        float overlap = (hardMinDist - dist) / std::max(0.001f, hardMinDist);
                        sample.blockedByMoving = true;
                        sample.softPenalty +=
                            (1.0f + overlap * 4.0f) * kMovingUnitPenaltyWeight;
                    } else {
                        sample.hardBlocked = true;
                        sample.blockedByMoving = sample.blockedByMoving || other.moving;
                        sample.blockedByIdle = sample.blockedByIdle || !other.moving;
                        return sample;
                    }
                }

                float preferredMinDist = preferredRadius + other.preferredRadius;
                float preferredMinDistSq = preferredMinDist * preferredMinDist;
                if (distSq < preferredMinDistSq && distSq > 0.0001f) {
                    float dist = std::sqrt(distSq);
                    float penalty = (preferredMinDist - dist) / preferredMinDist;
                    if (other.moving) {
                        penalty *= 1.25f;
                    }
                    sample.softPenalty += penalty;
                }
            }
        }
    }
    return sample;
}

inline float sampleDensityAhead(
    IREntity::EntityId self,
    float x,
    float y,
    float dirX,
    float dirY,
    float lookAheadDist
) {
    float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
    if (dirLen < 0.001f || lookAheadDist <= 0.001f) return 0.0f;

    float nx = dirX / dirLen;
    float ny = dirY / dirLen;
    float lateralLimit = std::max(s_unitCellSize, lookAheadDist * 0.5f);
    int cellX = static_cast<int>(std::floor(x / s_unitCellSize));
    int cellY = static_cast<int>(std::floor(y / s_unitCellSize));
    int cellRange = std::max(1, static_cast<int>(std::ceil(lookAheadDist / s_unitCellSize)) + 1);
    float density = 0.0f;

    for (int dx = -cellRange; dx <= cellRange; ++dx) {
        for (int dy = -cellRange; dy <= cellRange; ++dy) {
            auto it = s_unitHash.find(unitKey(cellX + dx, cellY + dy));
            if (it == s_unitHash.end()) continue;
            for (const auto &other : it->second) {
                if (other.entity_ == self) continue;

                float relX = other.x - x;
                float relY = other.y - y;
                float forward = relX * nx + relY * ny;
                if (forward <= 0.0f || forward > lookAheadDist) continue;

                float lateralSq = relX * relX + relY * relY - forward * forward;
                float lateral = std::sqrt(std::max(lateralSq, 0.0f));
                if (lateral > lateralLimit) continue;

                float forwardWeight = 1.0f - (forward / lookAheadDist);
                float lateralWeight = 1.0f - (lateral / lateralLimit);
                float occupancyWeight = other.moving ? 1.0f : 1.35f;
                density +=
                    std::max(0.0f, forwardWeight) *
                    std::max(0.0f, lateralWeight) *
                    occupancyWeight;
            }
        }
    }

    return density;
}

inline bool tryGetFieldDirection(
    const C_FlowField &flowField,
    const FlowFieldState &field,
    const C_NavWorld &navWorld,
    ivec3 worldCell,
    vec3 &directionOut
) {
    ChunkCoord chunkCoord = worldCellToChunkCoord(worldCell, navWorld.chunkSize_);
    const auto *chunk = flowField.getChunk(field, chunkCoord);
    if (!chunk) return false;

    ivec3 localCell = worldCellToLocalCell(worldCell, chunkCoord, navWorld.chunkSize_);
    int localIdx = localCell.x + localCell.y * navWorld.chunkSize_.x +
                   localCell.z * navWorld.chunkSize_.x * navWorld.chunkSize_.y;
    if (localIdx < 0 || localIdx >= static_cast<int>(chunk->directions_.size())) {
        return false;
    }
    if (!std::isfinite(chunk->costs_[static_cast<size_t>(localIdx)])) {
        return false;
    }

    ivec3 delta = chunk->directions_[static_cast<size_t>(localIdx)].toDelta();
    if (delta.x == 0 && delta.y == 0 && delta.z == 0) {
        return false;
    }

    vec3 dir(static_cast<float>(delta.x), static_cast<float>(delta.y), 0.0f);
    float len = IRMath::length(dir);
    if (len < 0.001f) return false;
    directionOut = dir / len;
    return true;
}

inline float getFieldCostAtCell(
    const C_FlowField &flowField,
    const FlowFieldState &field,
    const C_NavWorld &navWorld,
    ivec3 worldCell
);

inline float getFieldCostAtPosition(
    const C_FlowField &flowField,
    const FlowFieldState &field,
    const C_NavWorld &navWorld,
    float worldX,
    float worldY
) {
    ivec3 worldCell = navWorldToCell(navWorld, vec3(worldX, worldY, 0.0f));
    return getFieldCostAtCell(flowField, field, navWorld, worldCell);
}

inline float getFieldCostAtCell(
    const C_FlowField &flowField,
    const FlowFieldState &field,
    const C_NavWorld &navWorld,
    ivec3 worldCell
) {
    ChunkCoord chunkCoord = worldCellToChunkCoord(worldCell, navWorld.chunkSize_);
    const auto *chunk = flowField.getChunk(field, chunkCoord);
    if (!chunk) return std::numeric_limits<float>::infinity();

    ivec3 localCell = worldCellToLocalCell(worldCell, chunkCoord, navWorld.chunkSize_);
    int localIdx = localCell.x + localCell.y * navWorld.chunkSize_.x +
                   localCell.z * navWorld.chunkSize_.x * navWorld.chunkSize_.y;
    if (localIdx < 0 || localIdx >= static_cast<int>(chunk->costs_.size())) {
        return std::numeric_limits<float>::infinity();
    }
    return chunk->costs_[static_cast<size_t>(localIdx)];
}

inline bool tryGetFieldDirectionFromNeighbors(
    const C_FlowField &flowField,
    const FlowFieldState &field,
    const C_NavWorld &navWorld,
    ivec3 worldCell,
    vec3 &directionOut
) {
    float bestCost = std::numeric_limits<float>::infinity();
    bool found = false;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;

            ivec3 neighborCell = worldCell + ivec3(dx, dy, 0);
            vec3 neighborDirection;
            if (!tryGetFieldDirection(flowField, field, navWorld, neighborCell, neighborDirection)) {
                continue;
            }

            float neighborCost = getFieldCostAtCell(flowField, field, navWorld, neighborCell);
            if (neighborCost < bestCost) {
                bestCost = neighborCost;
                directionOut = neighborDirection;
                found = true;
            }
        }
    }

    return found;
}

struct NearbyUnitCount {
    int idle{0};
    int moving{0};
    int total() const { return idle + moving; }
    float idleRatio() const { return total() > 0 ? static_cast<float>(idle) / static_cast<float>(total()) : 0.0f; }
};

inline NearbyUnitCount countNearbyUnits(
    IREntity::EntityId self,
    float x,
    float y,
    float radius
) {
    NearbyUnitCount result;
    int cellX = static_cast<int>(std::floor(x / s_unitCellSize));
    int cellY = static_cast<int>(std::floor(y / s_unitCellSize));
    float radiusSq = radius * radius;

    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            auto it = s_unitHash.find(unitKey(cellX + dx, cellY + dy));
            if (it == s_unitHash.end()) continue;
            for (const auto &other : it->second) {
                if (other.entity_ == self) continue;
                float diffX = x - other.x;
                float diffY = y - other.y;
                if ((diffX * diffX + diffY * diffY) < radiusSq) {
                    if (other.moving) {
                        ++result.moving;
                    } else {
                        ++result.idle;
                    }
                }
            }
        }
    }

    return result;
}

inline int countIdleUnitsNearGoal(
    IREntity::EntityId self,
    float x,
    float y,
    float radius
) {
    return countNearbyUnits(self, x, y, radius).idle;
}

inline bool isBlockedBySettledUnitAhead(
    IREntity::EntityId self,
    float x,
    float y,
    float dirX,
    float dirY,
    float collisionRadius
) {
    float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
    if (dirLen < 0.001f) return false;
    float ndx = dirX / dirLen;
    float ndy = dirY / dirLen;

    float checkDist = collisionRadius * 3.0f;
    float aheadX = x + ndx * checkDist;
    float aheadY = y + ndy * checkDist;

    int cellX = static_cast<int>(std::floor(aheadX / s_unitCellSize));
    int cellY = static_cast<int>(std::floor(aheadY / s_unitCellSize));
    float checkRadiusSq = collisionRadius * collisionRadius * 4.0f;

    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            auto it = s_unitHash.find(unitKey(cellX + dx, cellY + dy));
            if (it == s_unitHash.end()) continue;
            for (const auto &other : it->second) {
                if (other.entity_ == self || other.moving) continue;
                float diffX = aheadX - other.x;
                float diffY = aheadY - other.y;
                if ((diffX * diffX + diffY * diffY) < checkRadiusSq) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace detail_flow_move

template <> struct System<FLOW_FIELD_MOVEMENT> {
    static SystemId create() {
        return createSystem<
            C_Position3D,
            C_SmoothMovement,
            C_NavAgent,
            C_ColliderCircle,
            C_FlowFieldAgent>(
            "FlowFieldMovement",
            [](IREntity::EntityId entity,
               C_Position3D &position,
               const C_SmoothMovement &,
               C_NavAgent &agent,
               const C_ColliderCircle &collider,
               C_FlowFieldAgent &flowAgent) {
                if (!flowAgent.hasField()) return;
                if (!detail_flow_move::s_navWorld ||
                    !detail_flow_move::s_registry ||
                    !detail_flow_move::s_flowField) {
                    return;
                }

                const auto &navWorld = *detail_flow_move::s_navWorld;
                const auto &registry = *detail_flow_move::s_registry;
                const auto *field = detail_flow_move::s_flowField->findField(flowAgent.fieldId_);
                if (!field) {
                    flowAgent.clear();
                    return;
                }

                float dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
                float cellSize = navWorld.cellSizeWorld_;
                vec3 current = position.pos_;

                float curCX = current.x + collider.centerOffset_.x;
                float curCY = current.y + collider.centerOffset_.y;
                if (navCircleOverlapsWall(navWorld, registry, curCX, curCY, 0.0f, collider.radius_)) {
                    constexpr int kEscapeRays = 16;
                    float escapeDist = cellSize * 0.5f;
                    for (int attempt = 0; attempt < 4; ++attempt) {
                        for (int i = 0; i < kEscapeRays; ++i) {
                            float angle = static_cast<float>(i) *
                                          (2.0f * 3.14159265f / static_cast<float>(kEscapeRays));
                            vec3 tryPos = current + vec3(
                                std::cos(angle) * escapeDist,
                                std::sin(angle) * escapeDist,
                                0.0f
                            );
                            float tryX = tryPos.x + collider.centerOffset_.x;
                            float tryY = tryPos.y + collider.centerOffset_.y;
                            if (!navCircleOverlapsWall(
                                    navWorld,
                                    registry,
                                    tryX,
                                    tryY,
                                    0.0f,
                                    collider.radius_)) {
                                position.pos_ = tryPos;
                                current = tryPos;
                                goto escaped;
                            }
                        }
                        escapeDist += cellSize * 0.5f;
                    }
                }
                escaped:;

                vec3 goalWorld = navCellToWorld(navWorld, registry, flowAgent.goalCell_);
                goalWorld.z = current.z;
                vec3 immediateDirection = detail_flow_move::buildImmediateDirection(current, goalWorld);
                flowAgent.immediateDirection_ = immediateDirection;

                ivec3 currentCell = navWorldToCell(navWorld, vec3(current.x, current.y, 0.0f));
                float goalDist = IRMath::length(vec2(goalWorld.x - current.x, goalWorld.y - current.y));
                float arriveRadius = flowAgent.exactGoal_
                    ? std::max(cellSize * 0.35f, collider.movementCollisionRadius_)
                    : std::max(
                        cellSize * 0.5f,
                        collider.movementCollisionRadius_ * 1.5f
                    );
                if (currentCell == flowAgent.goalCell_ || goalDist < arriveRadius) {
                    flowAgent.clear();
                    agent.stuckFrames_ = 0;
                    return;
                }

                vec3 groupCenterWorld = navCellToWorld(
                    navWorld, registry, flowAgent.groupCenterCell_
                );
                groupCenterWorld.z = current.z;

                if (flowAgent.bestGoalDist_ < 0.0f) {
                    flowAgent.bestGoalDist_ = goalDist;
                }
                if (goalDist < flowAgent.bestGoalDist_ - cellSize * 1.0f) {
                    flowAgent.bestGoalDist_ = goalDist;
                    flowAgent.noProgressFrames_ = 0;
                    flowAgent.unitBlockedFrames_ = 0;
                } else {
                    flowAgent.noProgressFrames_++;
                }

                float distToGroupCenter = IRMath::length(
                    vec2(groupCenterWorld.x - current.x, groupCenterWorld.y - current.y)
                );

                float unitWorldX = current.x + collider.centerOffset_.x;
                float unitWorldY = current.y + collider.centerOffset_.y;
                auto nearbyUnits = detail_flow_move::countNearbyUnits(
                    entity, unitWorldX, unitWorldY,
                    collider.preferredMovementRadius_ * 6.0f
                );
                float groupArriveRadius = arriveRadius +
                    std::sqrt(static_cast<float>(std::max(1, nearbyUnits.total()))) *
                        collider.movementCollisionRadius_ * 2.5f;

                constexpr int kMovingJamPatienceFrames = 150;
                constexpr int kWallRepathFrames = 45;
                constexpr int kForceSettleNoProgressFrames = 90;
                ivec3 repathTarget =
                    flowAgent.exactGoal_ ? flowAgent.goalCell_ : flowAgent.groupCenterCell_;

                if (!flowAgent.exactGoal_ &&
                    nearbyUnits.total() >= 2 &&
                    distToGroupCenter < groupArriveRadius &&
                    (nearbyUnits.idleRatio() >= 0.5f ||
                     flowAgent.noProgressFrames_ > 30)) {
                    flowAgent.clear();
                    agent.stuckFrames_ = 0;
                    return;
                }

                if (!flowAgent.exactGoal_ &&
                    flowAgent.noProgressFrames_ > 10 &&
                    distToGroupCenter < groupArriveRadius &&
                    detail_flow_move::isBlockedBySettledUnitAhead(
                        entity, unitWorldX, unitWorldY,
                        immediateDirection.x, immediateDirection.y,
                        collider.movementCollisionRadius_)) {
                    flowAgent.clear();
                    agent.stuckFrames_ = 0;
                    return;
                }

                if (flowAgent.noProgressFrames_ > kForceSettleNoProgressFrames) {
                    if (nearbyUnits.total() >= 1 &&
                        distToGroupCenter < groupArriveRadius) {
                        flowAgent.clear();
                    } else {
                        IREntity::setComponentDeferred(entity, C_MoveOrder(repathTarget));
                        flowAgent.clear();
                    }
                    agent.stuckFrames_ = 0;
                    return;
                }

                vec3 desiredDir(0.0f);
                bool hasFieldDirection = false;
                if (detail_flow_move::tryGetFieldDirection(
                        *detail_flow_move::s_flowField,
                        *field,
                        navWorld,
                        currentCell,
                        desiredDir)) {
                    flowAgent.waitingForField_ = false;
                    hasFieldDirection = true;
                } else if (detail_flow_move::tryGetFieldDirectionFromNeighbors(
                               *detail_flow_move::s_flowField,
                               *field,
                               navWorld,
                               currentCell,
                               desiredDir)) {
                    flowAgent.waitingForField_ = false;
                    hasFieldDirection = true;
                } else {
                    flowAgent.waitingForField_ = !field->complete_;
                    if (field->complete_ && IRMath::length(immediateDirection) < 0.001f) {
                        flowAgent.clear();
                        return;
                    }
                }

                if (field->complete_ && !hasFieldDirection) {
                    float costHere = detail_flow_move::getFieldCostAtCell(
                        *detail_flow_move::s_flowField,
                        *field,
                        navWorld,
                        currentCell
                    );
                    if (!std::isfinite(costHere)) {
                        flowAgent.clear();
                        agent.stuckFrames_ = 0;
                        return;
                    }
                }

                if (!hasFieldDirection && !field->complete_) {
                    float cx = current.x + collider.centerOffset_.x;
                    float cy = current.y + collider.centerOffset_.y;
                    float lookAhead = cellSize * 5.0f;
                    float aheadX = cx + immediateDirection.x * lookAhead;
                    float aheadY = cy + immediateDirection.y * lookAhead;
                    if (navCirclePathOverlapsWall(
                            navWorld, registry,
                            cx, cy, aheadX, aheadY,
                            0.0f, field->agentClearance_)) {
                        return;
                    }
                    desiredDir = immediateDirection;
                }

                if (IRMath::length(desiredDir) < 0.001f) {
                    desiredDir = immediateDirection;
                }
                if (IRMath::length(desiredDir) < 0.001f) {
                    return;
                }

                float moveDist = agent.moveSpeed_ * dt;
                if (goalDist < moveDist * 1.5f) {
                    moveDist = std::min(moveDist, std::max(goalDist * 0.6f, cellSize * 0.01f));
                }
                vec3 candidatePos = current;
                vec3 bestPos = current;
                float bestScore = std::numeric_limits<float>::infinity();
                bool foundCandidate = false;

                float preferredRadius = std::max(
                    collider.movementCollisionRadius_,
                    collider.preferredMovementRadius_
                );
                float preferredWallClearance = collider.radius_ + cellSize * 0.4f;
                vec3 perp(-desiredDir.y, desiredDir.x, 0.0f);
                float perpLen = IRMath::length(perp);
                if (perpLen > 0.001f) {
                    perp /= perpLen;
                } else {
                    perp = vec3(0.0f);
                }

                float preferredSide = ((static_cast<uint64_t>(entity) +
                                        static_cast<uint64_t>(agent.stuckFrames_ / 12)) &
                                       1ull)
                                          ? 1.0f
                                          : -1.0f;
                float dodgeScale = 0.75f + std::min(1.75f, static_cast<float>(agent.stuckFrames_) * 0.08f);
                float absDesiredX = std::abs(desiredDir.x);
                float absDesiredY = std::abs(desiredDir.y);
                bool hasAxisSlides = absDesiredX > 0.1f && absDesiredY > 0.1f;
                vec3 primarySlide(0.0f);
                vec3 secondarySlide(0.0f);
                if (hasAxisSlides) {
                    vec3 xSlide(desiredDir.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                    vec3 ySlide(0.0f, desiredDir.y > 0.0f ? 1.0f : -1.0f, 0.0f);
                    if (absDesiredX >= absDesiredY) {
                        primarySlide = xSlide;
                        secondarySlide = ySlide;
                    } else {
                        primarySlide = ySlide;
                        secondarySlide = xSlide;
                    }
                }

                int stuckFrames = agent.stuckFrames_;
                bool tryHalfSteps = stuckFrames > 0;
                bool sawWallBlock = false;
                bool sawMovingBlock = false;
                bool sawIdleBlock = false;
                bool bestCandidateCrowded = false;

                auto tryCandidate = [&](vec3 candidateDir, float sidePenalty) {
                    float dirLen = IRMath::length(candidateDir);
                    if (dirLen < 0.001f) return;
                    vec3 normDir = candidateDir / dirLen;
                    float fromX = current.x + collider.centerOffset_.x;
                    float fromY = current.y + collider.centerOffset_.y;

                    static constexpr float kStepScales[] = {1.0f, 0.5f, 0.25f};
                    int maxScales = tryHalfSteps ? 3 : 1;

                    for (int si = 0; si < maxScales; ++si) {
                        float tryDist = moveDist * kStepScales[si];
                        vec3 tryPos = current + normDir * tryDist;
                        float tryX = tryPos.x + collider.centerOffset_.x;
                        float tryY = tryPos.y + collider.centerOffset_.y;
                        if (navCirclePathOverlapsWall(
                                navWorld, registry,
                                fromX, fromY, tryX, tryY,
                                0.0f, collider.radius_)) {
                            sawWallBlock = true;
                            continue;
                        }

                        auto spacing = detail_flow_move::sampleUnitSpacing(
                            entity, tryX, tryY,
                            collider.movementCollisionRadius_,
                            preferredRadius
                        );
                        if (spacing.hardBlocked) {
                            sawIdleBlock = sawIdleBlock || spacing.blockedByIdle;
                            continue;
                        }
                        sawMovingBlock = sawMovingBlock || spacing.blockedByMoving;

                        float wallClearance = navGetClearanceAtWorld(
                            navWorld, registry, tryX, tryY, 0.0f
                        );
                        float wallPenalty = 0.0f;
                        if (wallClearance < preferredWallClearance) {
                            wallPenalty =
                                (preferredWallClearance - wallClearance) * cellSize * 2.5f;
                        }

                        float fieldCost = detail_flow_move::getFieldCostAtPosition(
                            *detail_flow_move::s_flowField, *field,
                            navWorld, tryX, tryY
                        );
                        if (!std::isfinite(fieldCost)) {
                            fieldCost = IRMath::length(vec2(
                                goalWorld.x - tryPos.x,
                                goalWorld.y - tryPos.y
                            )) / std::max(0.001f, cellSize);
                        }

                        float dist = IRMath::length(vec2(
                            goalWorld.x - tryPos.x,
                            goalWorld.y - tryPos.y
                        ));
                        float stepPenalty =
                            (si > 0) ? cellSize * 0.1f * static_cast<float>(si) : 0.0f;
                        float crowdPenalty =
                            detail_flow_move::sampleDensityAhead(
                                entity,
                                tryX,
                                tryY,
                                normDir.x,
                                normDir.y,
                                cellSize * 3.0f
                            ) *
                            cellSize * detail_flow_move::kCrowdLookAheadWeight;
                        float score =
                            fieldCost * cellSize +
                            dist * 0.15f +
                            spacing.softPenalty * cellSize * 2.5f +
                            crowdPenalty +
                            wallPenalty +
                            sidePenalty +
                            stepPenalty;
                        if (!foundCandidate || score < bestScore) {
                            foundCandidate = true;
                            bestScore = score;
                            bestPos = tryPos;
                            bestCandidateCrowded =
                                spacing.blockedByMoving ||
                                crowdPenalty > cellSize * 0.15f;
                        }
                        break;
                    }
                };

                tryCandidate(desiredDir, 0.0f);
                if (hasAxisSlides) {
                    tryCandidate(primarySlide, 0.0f);
                    tryCandidate(secondarySlide, cellSize * 0.03f);
                }
                tryCandidate(desiredDir + perp * (preferredSide * 0.45f * dodgeScale), cellSize * 0.02f);
                tryCandidate(desiredDir + perp * (-preferredSide * 0.45f * dodgeScale), cellSize * 0.08f);
                if (stuckFrames > 8) {
                    tryCandidate(desiredDir + perp * (preferredSide * 0.9f * dodgeScale), cellSize * 0.05f);
                    tryCandidate(desiredDir + perp * (-preferredSide * 0.9f * dodgeScale), cellSize * 0.12f);
                }
                if (stuckFrames > 16) {
                    tryCandidate(desiredDir + perp * (preferredSide * 1.35f * dodgeScale), cellSize * 0.1f);
                    tryCandidate(desiredDir + perp * (-preferredSide * 1.35f * dodgeScale), cellSize * 0.18f);
                }

                if (stuckFrames > 10 && IRMath::length(perp) > 0.001f) {
                    tryCandidate(perp * preferredSide, cellSize * 0.25f);
                    tryCandidate(perp * -preferredSide, cellSize * 0.35f);
                }

                float curClearance = navGetClearanceAtWorld(
                    navWorld, registry,
                    current.x + collider.centerOffset_.x,
                    current.y + collider.centerOffset_.y, 0.0f
                );
                if ((!foundCandidate || stuckFrames > 2) &&
                    curClearance < preferredWallClearance) {
                    float cx = current.x + collider.centerOffset_.x;
                    float cy = current.y + collider.centerOffset_.y;
                    float sampleDist = cellSize;
                    vec3 bestEscapeDir(0.0f);
                    float bestEscapeClear = curClearance;

                    static const vec3 escapeDirections[] = {
                        vec3(1, 0, 0),  vec3(-1, 0, 0),
                        vec3(0, 1, 0),  vec3(0, -1, 0),
                        vec3(0.707f, 0.707f, 0),   vec3(-0.707f, 0.707f, 0),
                        vec3(0.707f, -0.707f, 0),  vec3(-0.707f, -0.707f, 0)
                    };
                    for (const auto &dir : escapeDirections) {
                        float sampleClear = navGetClearanceAtWorld(
                            navWorld, registry,
                            cx + dir.x * sampleDist,
                            cy + dir.y * sampleDist, 0.0f
                        );
                        if (sampleClear > bestEscapeClear) {
                            bestEscapeClear = sampleClear;
                            bestEscapeDir = dir;
                        }
                    }

                    if (bestEscapeClear > curClearance) {
                        tryCandidate(bestEscapeDir, cellSize * 0.15f);
                    }
                }

                if (foundCandidate) {
                    candidatePos = bestPos;
                }

                if (!foundCandidate) {
                    if (goalDist < arriveRadius * 2.0f ||
                        distToGroupCenter < groupArriveRadius) {
                        flowAgent.clear();
                        agent.stuckFrames_ = 0;
                        return;
                    }

                    bool nearGroup = nearbyUnits.total() >= 2 &&
                        distToGroupCenter < groupArriveRadius * 1.5f;

                    if (sawMovingBlock || sawIdleBlock) {
                        flowAgent.unitBlockedFrames_++;
                        agent.stuckFrames_++;
                        if (nearGroup) {
                            flowAgent.clear();
                            agent.stuckFrames_ = 0;
                        } else if (flowAgent.unitBlockedFrames_ > kMovingJamPatienceFrames) {
                            IREntity::setComponentDeferred(entity, C_MoveOrder(repathTarget));
                            flowAgent.clear();
                            agent.stuckFrames_ = 0;
                        }
                        return;
                    }

                    flowAgent.unitBlockedFrames_ = 0;
                    agent.stuckFrames_++;
                    if (nearGroup) {
                        flowAgent.clear();
                        agent.stuckFrames_ = 0;
                        return;
                    }
                    if (agent.stuckFrames_ > kWallRepathFrames) {
                        IREntity::setComponentDeferred(entity, C_MoveOrder(repathTarget));
                        flowAgent.clear();
                        agent.stuckFrames_ = 0;
                    }
                    return;
                }

                float moveMag = IRMath::length(candidatePos - current);
                if (moveMag < moveDist * 0.1f) {
                    agent.stuckFrames_++;
                    if (bestCandidateCrowded) {
                        flowAgent.unitBlockedFrames_++;
                    } else {
                        flowAgent.unitBlockedFrames_ = 0;
                    }

                    bool nearGroup = nearbyUnits.total() >= 2 &&
                        distToGroupCenter < groupArriveRadius;
                    if (nearGroup &&
                        (nearbyUnits.idleRatio() >= 0.5f ||
                         flowAgent.noProgressFrames_ > 15)) {
                        flowAgent.clear();
                        agent.stuckFrames_ = 0;
                        return;
                    }
                    if (!nearGroup && bestCandidateCrowded &&
                        flowAgent.unitBlockedFrames_ > kMovingJamPatienceFrames) {
                        IREntity::setComponentDeferred(entity, C_MoveOrder(repathTarget));
                        flowAgent.clear();
                        agent.stuckFrames_ = 0;
                        return;
                    }
                    if (agent.stuckFrames_ > kWallRepathFrames) {
                        if (nearGroup) {
                            flowAgent.clear();
                        } else {
                            IREntity::setComponentDeferred(entity, C_MoveOrder(repathTarget));
                            flowAgent.clear();
                        }
                        agent.stuckFrames_ = 0;
                        return;
                    }
                } else {
                    agent.stuckFrames_ = 0;
                    flowAgent.unitBlockedFrames_ = 0;
                }

                position.pos_ = candidatePos;

                if (!flowAgent.exactGoal_ &&
                    moveMag < moveDist * 0.5f &&
                    nearbyUnits.total() >= 2 &&
                    distToGroupCenter < groupArriveRadius &&
                    flowAgent.noProgressFrames_ > 15) {
                    flowAgent.clear();
                    agent.stuckFrames_ = 0;
                }
            },
            []() {
                auto levelNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_NavWorld, C_ChunkRegistry, C_FlowField>()
                );
                if (!levelNodes.empty()) {
                    auto id = levelNodes[0]->entities_[0];
                    detail_flow_move::s_navWorld = &IREntity::getComponent<C_NavWorld>(id);
                    detail_flow_move::s_registry = &IREntity::getComponent<C_ChunkRegistry>(id);
                    detail_flow_move::s_flowField = &IREntity::getComponent<C_FlowField>(id);
                } else {
                    detail_flow_move::s_navWorld = nullptr;
                    detail_flow_move::s_registry = nullptr;
                    detail_flow_move::s_flowField = nullptr;
                }

                detail_flow_move::s_unitHash.clear();
                auto unitNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_Position3D, C_ColliderCircle, C_NavAgent>()
                );
                float maxRadius = 0.0f;
                for (auto *node : unitNodes) {
                    auto &colliders = IREntity::getComponentData<C_ColliderCircle>(node);
                    for (const auto &collider : colliders) {
                        if (collider.preferredMovementRadius_ > maxRadius) {
                            maxRadius = collider.preferredMovementRadius_;
                        }
                    }
                }
                detail_flow_move::s_unitCellSize = std::max(1.0f, maxRadius * 4.0f);

                for (auto *node : unitNodes) {
                    auto &positions = IREntity::getComponentData<C_Position3D>(node);
                    auto &colliders = IREntity::getComponentData<C_ColliderCircle>(node);
                    auto &agents = IREntity::getComponentData<C_NavAgent>(node);
                    for (size_t i = 0; i < positions.size(); ++i) {
                        float px = positions[i].pos_.x + colliders[i].centerOffset_.x;
                        float py = positions[i].pos_.y + colliders[i].centerOffset_.y;
                        int cx = static_cast<int>(std::floor(px / detail_flow_move::s_unitCellSize));
                        int cy = static_cast<int>(std::floor(py / detail_flow_move::s_unitCellSize));
                        bool moving = agents[i].hasPath();
                        if (auto flowAgent = IREntity::getComponentOptional<C_FlowFieldAgent>(
                                node->entities_[i])) {
                            moving = moving || (*flowAgent)->hasField();
                        }
                        detail_flow_move::s_unitHash[detail_flow_move::unitKey(cx, cy)].push_back(
                            {
                                node->entities_[i],
                                px,
                                py,
                                colliders[i].movementCollisionRadius_,
                                colliders[i].preferredMovementRadius_,
                                moving
                            }
                        );
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FLOW_FIELD_MOVEMENT_H */

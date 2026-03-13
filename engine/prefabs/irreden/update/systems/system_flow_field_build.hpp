#ifndef SYSTEM_FLOW_FIELD_BUILD_H
#define SYSTEM_FLOW_FIELD_BUILD_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>
#include <irreden/update/components/component_collider_circle.hpp>
#include <irreden/update/components/component_flow_field.hpp>
#include <irreden/update/components/component_flow_field_agent.hpp>
#include <irreden/update/components/component_flow_field_request.hpp>
#include <irreden/update/components/component_move_order.hpp>
#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/nav_query.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail_flow_build {

struct PendingAssignment {
    IREntity::EntityId entity_{IREntity::kNullEntity};
    vec3 position_{0.0f};
    ivec3 requestedGoalCell_{0, 0, 0};
    ivec3 goalCell_{0, 0, 0};
    ivec3 groupCenterCell_{0, 0, 0};
    float agentClearance_{0.5f};
    float planningClearance_{0.5f};
    float movementCollisionRadius_{0.5f};
    bool exactGoal_{false};
};

struct GoalGroupKey {
    ivec3 goalCell_{0, 0, 0};
    int clearanceMilli_{0};

    bool operator==(const GoalGroupKey &other) const {
        return goalCell_ == other.goalCell_ && clearanceMilli_ == other.clearanceMilli_;
    }
};

struct GoalGroupKeyHash {
    size_t operator()(const GoalGroupKey &key) const {
        size_t h1 = std::hash<int64_t>{}(posToKey(key.goalCell_));
        size_t h2 = std::hash<int>{}(key.clearanceMilli_);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

inline void addActiveFieldId(std::vector<int> &activeFieldIds, int fieldId) {
    if (fieldId == 0) return;
    if (std::find(activeFieldIds.begin(), activeFieldIds.end(), fieldId) != activeFieldIds.end()) {
        return;
    }
    activeFieldIds.push_back(fieldId);
}

inline vec3 buildImmediateDirection(vec3 from, vec3 to) {
    vec3 delta = to - from;
    delta.z = 0.0f;
    float len = IRMath::length(delta);
    if (len < 0.001f) return vec3(0.0f);
    return delta / len;
}

inline ivec3 resolveGoalCell(
    const C_NavWorld &navWorld,
    const C_ChunkRegistry &registry,
    ivec3 requestedGoal,
    float agentClearance
) {
    if (navIsPassable(navWorld, registry, requestedGoal, agentClearance)) {
        return requestedGoal;
    }

    ivec3 bestGoal = requestedGoal;
    float bestDist = std::numeric_limits<float>::infinity();
    int searchRadius = static_cast<int>(
        std::ceil(agentClearance / navWorld.cellSizeWorld_)) + 4;

    for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
        for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
            ivec3 candidate = requestedGoal + ivec3(dx, dy, 0);
            if (!navIsPassable(navWorld, registry, candidate, agentClearance)) continue;

            float dist = static_cast<float>(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestGoal = candidate;
            }
        }
    }

    return bestGoal;
}

inline float getFieldCostAt(
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

inline void setFieldCell(
    C_FlowField &flowField,
    FlowFieldState &field,
    const C_NavWorld &navWorld,
    ivec3 worldCell,
    float cost,
    FlowFieldDirection direction
) {
    ChunkCoord chunkCoord = worldCellToChunkCoord(worldCell, navWorld.chunkSize_);
    ivec3 localCell = worldCellToLocalCell(worldCell, chunkCoord, navWorld.chunkSize_);
    int localIdx = localCell.x + localCell.y * navWorld.chunkSize_.x +
                   localCell.z * navWorld.chunkSize_.x * navWorld.chunkSize_.y;
    if (localIdx < 0) return;

    auto &chunk = flowField.ensureChunk(field, chunkCoord, navWorld.chunkSize_);
    if (localIdx >= static_cast<int>(chunk.costs_.size())) return;

    chunk.costs_[static_cast<size_t>(localIdx)] = cost;
    chunk.directions_[static_cast<size_t>(localIdx)] = direction;
}

inline bool fieldHasDataForCell(
    const C_FlowField &flowField,
    const FlowFieldState &field,
    const C_NavWorld &navWorld,
    ivec3 worldCell
) {
    return std::isfinite(getFieldCostAt(flowField, field, navWorld, worldCell));
}

inline std::vector<ivec3> buildReachableGoals(
    const C_NavWorld &navWorld,
    const C_ChunkRegistry &registry,
    ivec3 goalCell,
    float clearance,
    int count,
    int spacingCells
) {
    std::vector<ivec3> goals;
    if (count <= 0) return goals;
    goals.reserve(static_cast<size_t>(count));

    struct Node {
        ivec3 cell;
        float cost;
        bool operator>(const Node &o) const { return cost > o.cost; }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> frontier;
    std::unordered_map<int64_t, float> visited;

    ivec3 resolvedGoal = goalCell;
    if (!navIsPassable(navWorld, registry, goalCell, clearance)) {
        resolvedGoal = resolveGoalCell(navWorld, registry, goalCell, clearance);
    }

    frontier.push({resolvedGoal, 0.0f});
    visited[posToKey(resolvedGoal)] = 0.0f;

    int maxExpansions = std::min(4096, count * spacingCells * spacingCells * 12);
    int expansions = 0;

    while (!frontier.empty() &&
           static_cast<int>(goals.size()) < count &&
           expansions < maxExpansions) {
        auto current = frontier.top();
        frontier.pop();

        auto it = visited.find(posToKey(current.cell));
        if (it != visited.end() && current.cost > it->second) continue;

        bool farEnough = true;
        for (const auto &g : goals) {
            if (std::max(
                    std::abs(current.cell.x - g.x),
                    std::abs(current.cell.y - g.y)) < spacingCells) {
                farEnough = false;
                break;
            }
        }
        if (farEnough) {
            goals.push_back(current.cell);
            if (static_cast<int>(goals.size()) >= count) break;
        }

        navForEachPassableNeighbor(
            navWorld, registry, current.cell, clearance,
            [&](ivec3 neighborCell) {
                float mc = moveCostForDelta(neighborCell - current.cell);
                float nextCost = current.cost + mc;
                auto nit = visited.find(posToKey(neighborCell));
                if (nit != visited.end() && nextCost >= nit->second) return;
                visited[posToKey(neighborCell)] = nextCost;
                frontier.push({neighborCell, nextCost});
            }
        );
        ++expansions;
    }

    while (static_cast<int>(goals.size()) < count) {
        goals.push_back(resolvedGoal);
    }
    return goals;
}

} // namespace detail_flow_build

template <> struct System<FLOW_FIELD_BUILD> {
    static SystemId create() {
        return createSystem<C_NavWorld, C_ChunkRegistry, C_FlowFieldRequest, C_FlowField>(
            "FlowFieldBuild",
            [](C_NavWorld &navWorld,
               C_ChunkRegistry &registry,
               C_FlowFieldRequest &requests,
               C_FlowField &flowField) {
                std::vector<detail_flow_build::PendingAssignment> assignments;
                std::vector<int> activeFieldIds;

                auto moveOrderNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_Position3D, C_NavAgent, C_MoveOrder, C_ColliderCircle>()
                );
                for (auto *node : moveOrderNodes) {
                    auto &positions = IREntity::getComponentData<C_Position3D>(node);
                    auto &agents = IREntity::getComponentData<C_NavAgent>(node);
                    auto &orders = IREntity::getComponentData<C_MoveOrder>(node);
                    auto &colliders = IREntity::getComponentData<C_ColliderCircle>(node);
                    for (int i = 0; i < node->length_; ++i) {
                        ivec3 requestedGoal = orders[i].targetCell_;
                        float planClearance = agents[i].planningClearance();
                        ivec3 targetCell = detail_flow_build::resolveGoalCell(
                            navWorld,
                            registry,
                            requestedGoal,
                            planClearance
                        );
                        detail_flow_build::PendingAssignment a;
                        a.entity_ = node->entities_[i];
                        a.position_ = positions[i].pos_;
                        a.requestedGoalCell_ = requestedGoal;
                        a.goalCell_ = targetCell;
                        a.groupCenterCell_ = targetCell;
                        a.agentClearance_ = agents[i].agentClearance_;
                        a.planningClearance_ = planClearance;
                        a.movementCollisionRadius_ = colliders[i].movementCollisionRadius_;
                        a.exactGoal_ = false;
                        assignments.push_back(a);
                        agents[i].clearPath();
                    }
                }

                std::unordered_map<
                    detail_flow_build::GoalGroupKey,
                    std::vector<size_t>,
                    detail_flow_build::GoalGroupKeyHash> groupedAssignments;
                for (size_t i = 0; i < assignments.size(); ++i) {
                    const auto &assignment = assignments[i];
                    groupedAssignments[{
                        assignment.goalCell_,
                        static_cast<int>(std::lround(assignment.planningClearance_ * 1000.0f))
                    }].push_back(i);
                }

                for (auto &[groupKey, indices] : groupedAssignments) {
                    if (indices.empty()) continue;

                    vec3 centerWorld = navCellToWorld(navWorld, registry, groupKey.goalCell_);
                    std::stable_sort(
                        indices.begin(),
                        indices.end(),
                        [&assignments, &centerWorld](size_t a, size_t b) {
                            float distA = IRMath::length(assignments[a].position_ - centerWorld);
                            float distB = IRMath::length(assignments[b].position_ - centerWorld);
                            return distA < distB;
                        }
                    );

                    float maxMovementRadius = 0.0f;
                    for (size_t index : indices) {
                        maxMovementRadius = std::max(
                            maxMovementRadius,
                            assignments[index].movementCollisionRadius_
                        );
                    }

                    int spacingCells = std::max(
                        1,
                        static_cast<int>(std::ceil(
                            (maxMovementRadius * 2.0f) / std::max(0.001f, navWorld.cellSizeWorld_)
                        ))
                    );
                    float groupPlanClearance = assignments[indices[0]].planningClearance_;
                    std::vector<ivec3> reachableGoals =
                        detail_flow_build::buildReachableGoals(
                            navWorld,
                            registry,
                            groupKey.goalCell_,
                            groupPlanClearance,
                            static_cast<int>(indices.size()),
                            spacingCells
                        );

                    int goalSlot = 0;
                    for (size_t slotIndex = 0; slotIndex < indices.size(); ++slotIndex) {
                        auto &assignment = assignments[indices[slotIndex]];
                        assignment.exactGoal_ = false;

                        bool canClaimExactTarget =
                            slotIndex == 0 &&
                            navIsPassable(
                                navWorld,
                                registry,
                                assignment.requestedGoalCell_,
                                groupPlanClearance);

                        if (canClaimExactTarget) {
                            assignment.goalCell_ = assignment.requestedGoalCell_;
                            assignment.exactGoal_ = true;
                        } else {
                            assignment.goalCell_ =
                                (goalSlot < static_cast<int>(reachableGoals.size()))
                                    ? reachableGoals[static_cast<size_t>(goalSlot)]
                                    : reachableGoals.back();
                            ++goalSlot;
                        }
                    }
                }

                for (const auto &assignment : assignments) {
                    int fieldId = requests.getOrCreateField(
                        assignment.goalCell_,
                        assignment.planningClearance_
                    );
                    detail_flow_build::addActiveFieldId(activeFieldIds, fieldId);

                    vec3 targetWorld = navCellToWorld(navWorld, registry, assignment.goalCell_);
                    targetWorld.z = assignment.position_.z;
                    IREntity::setComponent(
                        assignment.entity_,
                        C_FlowFieldAgent(
                            fieldId,
                            assignment.goalCell_,
                            assignment.groupCenterCell_,
                            detail_flow_build::buildImmediateDirection(
                                assignment.position_,
                                targetWorld
                            ),
                            assignment.exactGoal_
                        )
                    );
                    IREntity::removeComponent<C_MoveOrder>(assignment.entity_);
                }

                auto flowAgentNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_FlowFieldAgent>()
                );
                for (auto *node : flowAgentNodes) {
                    auto &flowAgents = IREntity::getComponentData<C_FlowFieldAgent>(node);
                    for (const auto &flowAgent : flowAgents) {
                        if (!flowAgent.hasField()) continue;
                        detail_flow_build::addActiveFieldId(activeFieldIds, flowAgent.fieldId_);
                    }
                }

                requests.pruneInactive(activeFieldIds);
                flowField.pruneInactive(activeFieldIds);

                std::vector<int> activeFields;
                activeFields.reserve(requests.requests_.size());
                for (auto &request : requests.requests_) {
                    auto &field = flowField.ensureField(
                        request.fieldId_,
                        request.goalCell_,
                        request.agentClearance_
                    );

                    if (request.dirty_ || field.dirty_) {
                        field.goalCell_ = detail_flow_build::resolveGoalCell(
                            navWorld,
                            registry,
                            request.goalCell_,
                            request.agentClearance_
                        );
                        field.agentClearance_ = request.agentClearance_;
                        field.complete_ = false;
                        field.dirty_ = false;
                        field.chunks_.clear();
                        field.frontier_ = {};

                        if (navIsPassable(
                                navWorld,
                                registry,
                                field.goalCell_,
                                field.agentClearance_)) {
                            detail_flow_build::setFieldCell(
                                flowField,
                                field,
                                navWorld,
                                field.goalCell_,
                                0.0f,
                                FlowFieldDirection{}
                            );
                            field.frontier_.push({0.0f, field.goalCell_});
                        } else {
                            field.complete_ = true;
                        }

                        request.dirty_ = false;
                    }

                    if (!field.complete_ && !field.frontier_.empty()) {
                        activeFields.push_back(field.fieldId_);
                    }
                }

                int remainingBudget = flowField.maxExpansionsPerFrame_;
                while (remainingBudget > 0 && !activeFields.empty()) {
                    bool progressed = false;

                    for (auto it = activeFields.begin(); it != activeFields.end() && remainingBudget > 0;) {
                        FlowFieldState *field = flowField.findField(*it);
                        if (!field) {
                            it = activeFields.erase(it);
                            continue;
                        }
                        if (field->frontier_.empty()) {
                            field->complete_ = true;
                            it = activeFields.erase(it);
                            continue;
                        }

                        FlowFieldFrontierNode current = field->frontier_.top();
                        field->frontier_.pop();

                        float currentCost = detail_flow_build::getFieldCostAt(
                            flowField,
                            *field,
                            navWorld,
                            current.cell_
                        );
                        if (current.cost_ > currentCost) {
                            progressed = true;
                            ++it;
                            continue;
                        }

                        navForEachPassableNeighbor(
                            navWorld,
                            registry,
                            current.cell_,
                            field->agentClearance_,
                            [&](ivec3 neighborCell) {
                                float baseCost = moveCostForDelta(neighborCell - current.cell_);

                                float neighborClearance = navGetCellClearance(
                                    navWorld, registry, neighborCell
                                );
                                float comfortClearance =
                                    field->agentClearance_ * 2.0f;
                                float wallProximity = 0.0f;
                                if (neighborClearance < comfortClearance &&
                                    comfortClearance > 0.0f) {
                                    float ratio =
                                        1.0f - neighborClearance / comfortClearance;
                                    wallProximity = ratio * ratio * 4.0f;
                                }

                                float nextCost = current.cost_ + baseCost + wallProximity;
                                float oldCost = detail_flow_build::getFieldCostAt(
                                    flowField,
                                    *field,
                                    navWorld,
                                    neighborCell
                                );
                                if (nextCost >= oldCost) return;

                                ivec3 direction = current.cell_ - neighborCell;
                                detail_flow_build::setFieldCell(
                                    flowField,
                                    *field,
                                    navWorld,
                                    neighborCell,
                                    nextCost,
                                    FlowFieldDirection(direction.x, direction.y, direction.z)
                                );
                                field->frontier_.push({nextCost, neighborCell});
                            }
                        );

                        --remainingBudget;
                        progressed = true;

                        if (field->frontier_.empty()) {
                            field->complete_ = true;
                            it = activeFields.erase(it);
                        } else {
                            ++it;
                        }
                    }

                    if (!progressed) {
                        break;
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FLOW_FIELD_BUILD_H */

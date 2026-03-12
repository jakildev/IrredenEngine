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
#include <unordered_set>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail_flow_build {

struct PendingAssignment {
    IREntity::EntityId entity_{IREntity::kNullEntity};
    vec3 position_{0.0f};
    ivec3 goalCell_{0, 0, 0};
    ivec3 groupCenterCell_{0, 0, 0};
    float agentClearance_{0.5f};
    float movementCollisionRadius_{0.5f};
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

inline std::vector<ivec3> buildFormationOffsets(int count, int spacingCells) {
    std::vector<ivec3> offsets;
    offsets.reserve(static_cast<size_t>(count));
    offsets.push_back(ivec3(0, 0, 0));

    for (int ring = 1; static_cast<int>(offsets.size()) < count; ++ring) {
        for (int dy = -ring; dy <= ring && static_cast<int>(offsets.size()) < count; ++dy) {
            for (int dx = -ring; dx <= ring && static_cast<int>(offsets.size()) < count; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                offsets.push_back(ivec3(dx * spacingCells, dy * spacingCells, 0));
            }
        }
    }

    return offsets;
}

inline ivec3 resolveUniqueGoalCell(
    const C_NavWorld &navWorld,
    const C_ChunkRegistry &registry,
    ivec3 requestedGoal,
    float agentClearance,
    const std::unordered_set<int64_t> &usedGoalKeys
) {
    if (navIsPassable(navWorld, registry, requestedGoal, agentClearance) &&
        usedGoalKeys.find(posToKey(requestedGoal)) == usedGoalKeys.end()) {
        return requestedGoal;
    }

    ivec3 bestGoal = requestedGoal;
    float bestDist = std::numeric_limits<float>::infinity();
    bool found = false;
    int searchRadius = static_cast<int>(
        std::ceil(agentClearance / navWorld.cellSizeWorld_)) + 6;

    for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
            ivec3 candidate = requestedGoal + ivec3(dx, dy, 0);
            if (!navIsPassable(navWorld, registry, candidate, agentClearance)) continue;
            if (usedGoalKeys.find(posToKey(candidate)) != usedGoalKeys.end()) continue;

            float dist = static_cast<float>(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestGoal = candidate;
                found = true;
            }
        }
    }

    return found ? bestGoal : resolveGoalCell(navWorld, registry, requestedGoal, agentClearance);
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
                        ivec3 targetCell = detail_flow_build::resolveGoalCell(
                            navWorld,
                            registry,
                            orders[i].targetCell_,
                            agents[i].agentClearance_
                        );
                        assignments.push_back({
                            node->entities_[i],
                            positions[i].pos_,
                            targetCell,
                            targetCell,
                            agents[i].agentClearance_,
                            colliders[i].movementCollisionRadius_
                        });
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
                        static_cast<int>(std::lround(assignment.agentClearance_ * 1000.0f))
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
                    std::vector<ivec3> offsets = detail_flow_build::buildFormationOffsets(
                        static_cast<int>(indices.size()),
                        spacingCells
                    );

                    std::unordered_set<int64_t> usedGoalKeys;
                    for (size_t slotIndex = 0; slotIndex < indices.size(); ++slotIndex) {
                        auto &assignment = assignments[indices[slotIndex]];
                        ivec3 desiredGoal = groupKey.goalCell_ + offsets[slotIndex];
                        assignment.goalCell_ = detail_flow_build::resolveUniqueGoalCell(
                            navWorld,
                            registry,
                            desiredGoal,
                            assignment.agentClearance_,
                            usedGoalKeys
                        );
                        usedGoalKeys.insert(posToKey(assignment.goalCell_));
                    }
                }

                for (const auto &assignment : assignments) {
                    int fieldId = requests.getOrCreateField(
                        assignment.goalCell_,
                        assignment.agentClearance_
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
                            )
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

                std::vector<FlowFieldState *> activeFields;
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
                        activeFields.push_back(&field);
                    }
                }

                int remainingBudget = flowField.maxExpansionsPerFrame_;
                while (remainingBudget > 0 && !activeFields.empty()) {
                    bool progressed = false;

                    for (auto it = activeFields.begin(); it != activeFields.end() && remainingBudget > 0;) {
                        FlowFieldState *field = *it;
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
                                float nextCost =
                                    current.cost_ + moveCostForDelta(neighborCell - current.cell_);
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

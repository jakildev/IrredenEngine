#ifndef SYSTEM_TURN_TO_MOVE_H
#define SYSTEM_TURN_TO_MOVE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/update/nav_query.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_facing_2d.hpp>
#include <irreden/update/components/component_flow_field.hpp>
#include <irreden/update/components/component_flow_field_agent.hpp>
#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>

#include <cmath>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail_turn {
    static const C_NavWorld *s_navWorld = nullptr;
    static const C_ChunkRegistry *s_registry = nullptr;
    static const C_FlowField *s_flowField = nullptr;
}

template <> struct System<TURN_TO_MOVE> {
    static SystemId create() {
        return createSystem<C_Position3D, C_Facing2D, C_NavAgent>(
            "TurnToMove",
            [](IREntity::EntityId entity,
               C_Position3D &pos,
               C_Facing2D &facing,
               C_NavAgent &agent) {
                if (!detail_turn::s_navWorld) return;

                vec3 delta(0.0f);
                auto flowAgent = IREntity::getComponentOptional<C_FlowFieldAgent>(entity);
                if (flowAgent && (*flowAgent)->hasField() && detail_turn::s_flowField) {
                    const auto *field = detail_turn::s_flowField->findField((*flowAgent)->fieldId_);
                    ivec3 currentCell = navWorldToCell(
                        *detail_turn::s_navWorld,
                        vec3(pos.pos_.x, pos.pos_.y, 0.0f)
                    );
                    bool hasFlowDir = false;
                    if (field) {
                        ChunkCoord chunkCoord = worldCellToChunkCoord(
                            currentCell,
                            detail_turn::s_navWorld->chunkSize_
                        );
                        const auto *chunk = detail_turn::s_flowField->getChunk(*field, chunkCoord);
                        if (chunk) {
                            ivec3 localCell = worldCellToLocalCell(
                                currentCell,
                                chunkCoord,
                                detail_turn::s_navWorld->chunkSize_
                            );
                            int localIdx = localCell.x +
                                           localCell.y * detail_turn::s_navWorld->chunkSize_.x +
                                           localCell.z * detail_turn::s_navWorld->chunkSize_.x *
                                               detail_turn::s_navWorld->chunkSize_.y;
                            if (localIdx >= 0 &&
                                localIdx < static_cast<int>(chunk->directions_.size()) &&
                                std::isfinite(chunk->costs_[static_cast<size_t>(localIdx)])) {
                                ivec3 dir = chunk->directions_[static_cast<size_t>(localIdx)].toDelta();
                                if (dir != ivec3(0, 0, 0)) {
                                    delta = vec3(
                                        static_cast<float>(dir.x),
                                        static_cast<float>(dir.y),
                                        0.0f
                                    );
                                    hasFlowDir = true;
                                }
                            }
                        }
                    }

                    if (!hasFlowDir) {
                        vec3 targetWorld = navCellToWorld(
                            *detail_turn::s_navWorld,
                            *detail_turn::s_registry,
                            (*flowAgent)->goalCell_
                        );
                        targetWorld.z = pos.pos_.z;
                        delta = targetWorld - pos.pos_;
                    }
                } else {
                    if (!agent.hasPath()) return;
                    ivec3 targetCellPos = agent.path_[static_cast<size_t>(agent.pathIndex_)];
                    vec3 targetWorld = navCellToWorld(
                        *detail_turn::s_navWorld, *detail_turn::s_registry, targetCellPos);
                    targetWorld.z = pos.pos_.z;
                    delta = targetWorld - pos.pos_;
                }

                float dist = IRMath::length(vec2(delta.x, delta.y));
                if (dist < 0.001f) return;

                float desiredAngle = std::atan2(delta.y, delta.x);
                float diff = desiredAngle - facing.angle_;

                constexpr float kPi = 3.14159265f;
                while (diff > kPi) diff -= 2.0f * kPi;
                while (diff < -kPi) diff += 2.0f * kPi;

                float dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
                float maxTurn = facing.turnRate_ * dt;

                if (std::abs(diff) <= maxTurn) {
                    facing.angle_ = desiredAngle;
                } else {
                    facing.angle_ += (diff > 0.0f ? maxTurn : -maxTurn);
                }

                while (facing.angle_ > kPi) facing.angle_ -= 2.0f * kPi;
                while (facing.angle_ < -kPi) facing.angle_ += 2.0f * kPi;
            },
            []() {
                auto nodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_NavWorld, C_ChunkRegistry, C_FlowField>()
                );
                if (!nodes.empty()) {
                    auto id = nodes[0]->entities_[0];
                    detail_turn::s_navWorld =
                        &IREntity::getComponent<C_NavWorld>(id);
                    detail_turn::s_registry =
                        &IREntity::getComponent<C_ChunkRegistry>(id);
                    detail_turn::s_flowField =
                        &IREntity::getComponent<C_FlowField>(id);
                } else {
                    detail_turn::s_navWorld = nullptr;
                    detail_turn::s_registry = nullptr;
                    detail_turn::s_flowField = nullptr;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TURN_TO_MOVE_H */

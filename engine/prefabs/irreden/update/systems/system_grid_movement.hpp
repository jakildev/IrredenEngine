#ifndef SYSTEM_GRID_MOVEMENT_H
#define SYSTEM_GRID_MOVEMENT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/update/nav_query.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>
#include <irreden/common/components/component_smooth_movement.hpp>
#include <irreden/update/components/component_move_order.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail_gridmove {
    static const C_NavWorld *s_navWorld = nullptr;
    static const C_ChunkRegistry *s_registry = nullptr;
}

template <> struct System<GRID_MOVEMENT> {
    static SystemId create() {
        return createSystem<C_Position3D, C_PositionGlobal3D, C_NavAgent>(
            "GridMovement",
            [](const Archetype &,
               std::vector<IREntity::EntityId> &entities,
               std::vector<C_Position3D> &positions,
               std::vector<C_PositionGlobal3D> &positionsGlobal,
               std::vector<C_NavAgent> &agents) {
                if (!detail_gridmove::s_navWorld) return;

                const auto &nw = *detail_gridmove::s_navWorld;
                const auto &reg = *detail_gridmove::s_registry;

                float dt = static_cast<float>(IRTime::deltaTime(IRTime::RENDER));

                for (size_t i = 0; i < entities.size(); i++) {
                    if (IREntity::getComponentOptional<C_SmoothMovement>(entities[i]).has_value()) {
                        continue;
                    }
                    C_NavAgent &agent = agents[i];
                    if (!agent.hasPath()) continue;

                    C_Position3D &pos = positions[i];
                    ivec3 targetCellPos = agent.path_[static_cast<size_t>(agent.pathIndex_)];
                    vec3 targetWorld = navCellToWorld(nw, reg, targetCellPos);
                    targetWorld.z = pos.pos_.z;

                    vec3 current = pos.pos_;
                    vec3 delta = targetWorld - current;
                    float dist = IRMath::length(delta);
                    float moveDist = agent.moveSpeed_ * dt;

                    if (dist <= moveDist || dist < 0.01f) {
                        pos.pos_ = targetWorld;
                        agent.pathIndex_++;
                        if (agent.pathIndex_ >= static_cast<int>(agent.path_.size())) {
                            if (agent.partialPath_) {
                                IREntity::setComponentDeferred(
                                    entities[i],
                                    C_MoveOrder(agent.finalTarget_)
                                );
                            }
                            agent.clearPath();
                        }
                    } else {
                        vec3 dir = IRMath::normalize(delta);
                        pos.pos_ = current + dir * moveDist;
                    }
                }
            },
            []() {
                auto nodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_NavWorld, C_ChunkRegistry>()
                );
                if (!nodes.empty()) {
                    auto id = nodes[0]->entities_[0];
                    detail_gridmove::s_navWorld =
                        &IREntity::getComponent<C_NavWorld>(id);
                    detail_gridmove::s_registry =
                        &IREntity::getComponent<C_ChunkRegistry>(id);
                } else {
                    detail_gridmove::s_navWorld = nullptr;
                    detail_gridmove::s_registry = nullptr;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GRID_MOVEMENT_H */

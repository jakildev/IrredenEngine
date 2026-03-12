#ifndef SYSTEM_GRID_PATHFIND_H
#define SYSTEM_GRID_PATHFIND_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/update/nav_query.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>
#include <irreden/update/components/component_move_order.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail_pathfind {
    static const C_NavWorld *s_navWorld = nullptr;
    static const C_ChunkRegistry *s_registry = nullptr;
}

template <> struct System<GRID_PATHFIND> {
    static SystemId create() {
        return createSystem<C_PositionGlobal3D, C_NavAgent, C_MoveOrder>(
            "GridPathfind",
            [](IREntity::EntityId entity,
               C_PositionGlobal3D &position,
               C_NavAgent &agent,
               C_MoveOrder &moveOrder) {
                if (!detail_pathfind::s_navWorld) return;

                const auto &nw = *detail_pathfind::s_navWorld;
                const auto &reg = *detail_pathfind::s_registry;

                vec3 posOnGround(position.pos_.x, position.pos_.y, 0.0f);
                ivec3 start = navWorldToCell(nw, posOnGround);
                ivec3 end = moveOrder.targetCell_;

                std::vector<ivec3> path = findPathAStarChunked(
                    nw, reg, start, end, agent.agentClearance_
                );
                if (!path.empty()) {
                    agent.setPath(path, end);
                }

                IREntity::removeComponentDeferred<C_MoveOrder>(entity);
            },
            []() {
                auto nodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_NavWorld, C_ChunkRegistry>()
                );
                if (!nodes.empty()) {
                    auto id = nodes[0]->entities_[0];
                    detail_pathfind::s_navWorld =
                        &IREntity::getComponent<C_NavWorld>(id);
                    detail_pathfind::s_registry =
                        &IREntity::getComponent<C_ChunkRegistry>(id);
                } else {
                    detail_pathfind::s_navWorld = nullptr;
                    detail_pathfind::s_registry = nullptr;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GRID_PATHFIND_H */

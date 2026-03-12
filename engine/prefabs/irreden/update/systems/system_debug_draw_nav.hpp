#ifndef SYSTEM_DEBUG_DRAW_NAV_H
#define SYSTEM_DEBUG_DRAW_NAV_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/update/nav_query.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/update/components/component_collider_circle.hpp>
#include <irreden/update/components/component_facing_2d.hpp>
#include <irreden/update/components/component_flow_field_agent.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>
#include <irreden/render/systems/system_debug_overlay.hpp>

#include <algorithm>
#include <cmath>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail_debug_nav {
    static const C_NavWorld *s_navWorld = nullptr;
    static const C_ChunkRegistry *s_registry = nullptr;

    inline void drawFacingIndicator(vec3 center, const C_ColliderCircle &collider, const C_Facing2D &facing) {
        const float indicatorLength = std::max(collider.radius_ * 1.45f, 0.15f);
        const float arrowLength = indicatorLength * 0.32f;

        const vec3 forward{
            std::cos(facing.angle_),
            std::sin(facing.angle_),
            0.0f
        };
        const vec3 right{-forward.y, forward.x, 0.0f};
        const vec3 tip = center + forward * indicatorLength;
        const vec3 base = tip - forward * arrowLength;
        const vec3 leftWing = base + right * (arrowLength * 0.6f);
        const vec3 rightWing = base - right * (arrowLength * 0.6f);

        IRDebug::drawLine3D(center, tip, 1.0f, 0.95f, 0.25f, 0.95f);
        IRDebug::drawLine3D(leftWing, tip, 1.0f, 0.95f, 0.25f, 0.95f);
        IRDebug::drawLine3D(rightWing, tip, 1.0f, 0.95f, 0.25f, 0.95f);
    }

    /*
    inline void drawCellOutline(vec3 center, float radius, float alpha = 0.18f) {
        const vec3 xPos = center + vec3(radius, 0.0f, 0.0f);
        const vec3 yPos = center + vec3(0.0f, radius, 0.0f);
        const vec3 xNeg = center + vec3(-radius, 0.0f, 0.0f);
        const vec3 yNeg = center + vec3(0.0f, -radius, 0.0f);

        IRDebug::drawLine3D(xPos, yPos, 0.0f, 0.0f, 0.0f, alpha);
        IRDebug::drawLine3D(yPos, xNeg, 0.0f, 0.0f, 0.0f, alpha);
        IRDebug::drawLine3D(xNeg, yNeg, 0.0f, 0.0f, 0.0f, alpha);
        IRDebug::drawLine3D(yNeg, xPos, 0.0f, 0.0f, 0.0f, alpha);
    }

    inline void drawNavBoard(const C_NavWorld &navWorld, const C_ChunkRegistry &registry) {
        const float halfCell = navWorld.cellSizeWorld_ * 0.5f;

        for (const auto &[_, chunkEntry] : registry.coordToChunk_) {
            const C_NavChunkData *chunk = chunkEntry.data_;
            if (!chunk) continue;

            for (int z = 0; z < chunk->chunkSize_.z; ++z) {
                for (int y = 0; y < chunk->chunkSize_.y; ++y) {
                    for (int x = 0; x < chunk->chunkSize_.x; ++x) {
                        const ivec3 localCell{x, y, z};
                        const int localIdx = chunk->localPosToIndex(localCell);
                        if (localIdx < 0 || !chunk->exists_[static_cast<size_t>(localIdx)]) {
                            continue;
                        }

                        const vec3 cellWorld = chunk->localCellToWorld(localCell);
                        const ivec3 worldCell = navWorldToCell(navWorld, cellWorld);
                        const bool walkable = chunk->walkable_[static_cast<size_t>(localIdx)];
                        const bool checkerOdd = ((worldCell.x + worldCell.y) & 1) != 0;

                        float r = 0.18f;
                        float g = 0.55f;
                        float b = 0.28f;
                        float a = checkerOdd ? 0.18f : 0.28f;

                        if (!walkable) {
                            r = checkerOdd ? 0.42f : 0.55f;
                            g = checkerOdd ? 0.15f : 0.18f;
                            b = checkerOdd ? 0.15f : 0.18f;
                            a = checkerOdd ? 0.22f : 0.32f;
                        }

                        IRDebug::drawDiamond3D(cellWorld, halfCell, r, g, b, a);
                        drawCellOutline(cellWorld, halfCell);
                    }
                }
            }
        }
    }
    */
}

template <> struct System<DEBUG_DRAW_NAV> {
    static SystemId create() {
        return createSystem<C_Position3D, C_NavAgent, C_ColliderCircle>(
            "DebugDrawNav",
            [](IREntity::EntityId entity,
               C_Position3D &pos,
               C_NavAgent &agent,
               const C_ColliderCircle &collider) {
                // Draw collider circle (green)
                vec3 circleCenter = pos.pos_ + vec3(collider.centerOffset_.x, collider.centerOffset_.y, 0.0f);
                IRDebug::drawCircle3D(circleCenter, collider.radius_, 0.0f, 1.0f, 0.0f, 0.8f, 24);
                IRDebug::drawCircle3D(
                    circleCenter,
                    collider.movementCollisionRadius_,
                    1.0f,
                    1.0f,
                    0.0f,
                    0.8f,
                    16
                );

                auto facing = IREntity::getComponentOptional<C_Facing2D>(entity);
                if (facing) {
                    detail_debug_nav::drawFacingIndicator(circleCenter, collider, *(*facing));
                }

                // Draw path (red)
                if (detail_debug_nav::s_navWorld) {
                    auto flowAgent = IREntity::getComponentOptional<C_FlowFieldAgent>(entity);
                    if (flowAgent && (*flowAgent)->hasField()) {
                        vec3 goalWorld = navCellToWorld(
                            *detail_debug_nav::s_navWorld,
                            *detail_debug_nav::s_registry,
                            (*flowAgent)->goalCell_
                        );
                        goalWorld.z = pos.pos_.z;
                        IRDebug::drawLine3D(pos.pos_, goalWorld, 0.2f, 0.6f, 1.0f, 0.8f);
                    }
                }

                if (!agent.hasPath() || !detail_debug_nav::s_navWorld) return;

                const auto &nw = *detail_debug_nav::s_navWorld;
                const auto &reg = *detail_debug_nav::s_registry;

                vec3 prev = pos.pos_;
                for (int i = agent.pathIndex_; i < static_cast<int>(agent.path_.size()); i++) {
                    vec3 wp = navCellToWorld(nw, reg, agent.path_[static_cast<size_t>(i)]);
                    wp.z = pos.pos_.z;
                    IRDebug::drawLine3D(prev, wp, 1.0f, 0.2f, 0.2f, 0.9f);
                    prev = wp;
                }
            },
            []() {
                auto nodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_NavWorld, C_ChunkRegistry>()
                );
                if (!nodes.empty()) {
                    auto id = nodes[0]->entities_[0];
                    detail_debug_nav::s_navWorld =
                        &IREntity::getComponent<C_NavWorld>(id);
                    detail_debug_nav::s_registry =
                        &IREntity::getComponent<C_ChunkRegistry>(id);
                    // detail_debug_nav::drawNavBoard(
                    //     *detail_debug_nav::s_navWorld,
                    //     *detail_debug_nav::s_registry
                    // );
                } else {
                    detail_debug_nav::s_navWorld = nullptr;
                    detail_debug_nav::s_registry = nullptr;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_DEBUG_DRAW_NAV_H */

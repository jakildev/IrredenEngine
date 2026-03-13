#ifndef SYSTEM_DEBUG_DRAW_NAV_H
#define SYSTEM_DEBUG_DRAW_NAV_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/update/nav_query.hpp>
#include <irreden/common/components/component_name.hpp>
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

}

template <> struct System<DEBUG_DRAW_NAV> {
    static SystemId create() {
        return createSystem<C_Name>(
            "DebugDrawNav",
            [](const C_Name &) {},
            nullptr,
            []() {
                const C_NavWorld *navWorld = nullptr;
                const C_ChunkRegistry *registry = nullptr;

                auto levelNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_NavWorld, C_ChunkRegistry>()
                );
                if (!levelNodes.empty()) {
                    auto id = levelNodes[0]->entities_[0];
                    navWorld = &IREntity::getComponent<C_NavWorld>(id);
                    registry = &IREntity::getComponent<C_ChunkRegistry>(id);
                }

                auto nodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_Position3D, C_NavAgent, C_ColliderCircle>()
                );

                const ComponentId facingId = IREntity::getComponentType<C_Facing2D>();
                const ComponentId flowFieldId = IREntity::getComponentType<C_FlowFieldAgent>();

                for (auto *node : nodes) {
                    auto &positions = IREntity::getComponentData<C_Position3D>(node);
                    auto &agents = IREntity::getComponentData<C_NavAgent>(node);
                    auto &colliders = IREntity::getComponentData<C_ColliderCircle>(node);

                    const bool hasFacing = node->type_.count(facingId) > 0;
                    const bool hasFlowField = node->type_.count(flowFieldId) > 0;

                    std::vector<C_Facing2D> *facingVec = nullptr;
                    std::vector<C_FlowFieldAgent> *flowFieldVec = nullptr;
                    if (hasFacing) {
                        facingVec = &IREntity::getComponentData<C_Facing2D>(node);
                    }
                    if (hasFlowField) {
                        flowFieldVec = &IREntity::getComponentData<C_FlowFieldAgent>(node);
                    }

                    for (int i = 0; i < node->length_; ++i) {
                        const auto &pos = positions[i];
                        const auto &agent = agents[i];
                        const auto &collider = colliders[i];

                        vec3 circleCenter = pos.pos_ + vec3(
                            collider.centerOffset_.x,
                            collider.centerOffset_.y,
                            0.0f
                        );

                        IRDebug::drawCircle3D(circleCenter, collider.radius_, 0.0f, 1.0f, 0.0f, 0.8f, 12);
                        IRDebug::drawCircle3D(
                            circleCenter,
                            collider.movementCollisionRadius_,
                            1.0f, 1.0f, 0.0f, 0.8f, 8
                        );

                        if (hasFacing) {
                            detail_debug_nav::drawFacingIndicator(
                                circleCenter, collider, (*facingVec)[i]
                            );
                        }

                        if (navWorld && hasFlowField && (*flowFieldVec)[i].hasField()) {
                            vec3 goalWorld = navCellToWorld(
                                *navWorld, *registry, (*flowFieldVec)[i].goalCell_
                            );
                            goalWorld.z = pos.pos_.z;
                            IRDebug::drawLine3D(pos.pos_, goalWorld, 0.2f, 0.6f, 1.0f, 0.8f);
                        }

                        if (!agent.hasPath() || !navWorld) continue;

                        vec3 prev = pos.pos_;
                        for (int p = agent.pathIndex_; p < static_cast<int>(agent.path_.size()); p++) {
                            vec3 wp = navCellToWorld(*navWorld, *registry, agent.path_[static_cast<size_t>(p)]);
                            wp.z = pos.pos_.z;
                            IRDebug::drawLine3D(prev, wp, 1.0f, 0.2f, 0.2f, 0.9f);
                            prev = wp;
                        }
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_DEBUG_DRAW_NAV_H */

#ifndef IR_VOXEL_EDITOR_CONSTRUCTION_GRID_H
#define IR_VOXEL_EDITOR_CONSTRUCTION_GRID_H

#include "editor_picking.hpp"

#include <irreden/render/debug_overlay_draws.hpp>

namespace IRVoxelEditor {

inline void drawConstructionGrid(IREntity::EntityId target, bool enabled) {
    if (!enabled || target == IREntity::kNullEntity || !isEditable(target))
        return;
    const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(target);
    if (set.globalPositions_.empty())
        return;
    const IRMath::vec3 origin = IRMath::vec3(IRMath::roundVec3HalfUp(set.globalPositions_[0].pos_));
    const float floor = static_cast<float>(set.size_.z - 1);
    const auto screen = [](IRMath::vec3 point) {
        IRMath::vec2 result = IRRender::windowPointsToFramebufferPx(
            IRMath::vec2(IRRender::worldPos3DToMouseScreenPxExact(point))
        );
        result.y = static_cast<float>(IRRender::getViewport().y) - result.y;
        return result;
    };
    const auto line = [&](IRMath::vec3 from, IRMath::vec3 to) {
        IRDebug::drawLineScreen(
            screen(origin + from),
            screen(origin + to),
            0.28f,
            0.38f,
            0.46f,
            0.6f
        );
    };
    for (int x = 0; x <= set.size_.x; ++x)
        line(
            {static_cast<float>(x) - 0.5f, -0.5f, floor},
            {static_cast<float>(x) - 0.5f, static_cast<float>(set.size_.y) - 0.5f, floor}
        );
    for (int y = 0; y <= set.size_.y; ++y)
        line(
            {-0.5f, static_cast<float>(y) - 0.5f, floor},
            {static_cast<float>(set.size_.x) - 0.5f, static_cast<float>(y) - 0.5f, floor}
        );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_CONSTRUCTION_GRID_H */

#ifndef IR_VOXEL_EDITOR_CONSTRUCTION_GRID_H
#define IR_VOXEL_EDITOR_CONSTRUCTION_GRID_H

#include "editor_picking.hpp"

#include <irreden/render/depth_tested_primitives.hpp>

namespace IRVoxelEditor {

// The +Z-down support plane is the bottom face of the last editable layer.
// XY grid boundaries likewise lie half a cell from integer voxel centers.
inline void appendConstructionGridLines(
    IRMath::vec3 origin,
    IRMath::ivec3 size,
    std::vector<IRPrefab::WorldPrimitiveVertex> &lines,
    float viewZ = 1.0f
) {
    const float floor = static_cast<float>(size.z) - 0.5f;
    const float opacity = IRMath::mix(0.12f, 0.6f, IRMath::clamp(viewZ * 3.0f, 0.0f, 1.0f));
    const auto line = [&](IRMath::vec3 from, IRMath::vec3 to) {
        const IRMath::vec4 color(0.28f, 0.38f, 0.46f, opacity);
        lines.push_back({origin + from, color});
        lines.push_back({origin + to, color});
    };
    for (int x = 0; x <= size.x; ++x)
        line(
            {static_cast<float>(x) - 0.5f, -0.5f, floor},
            {static_cast<float>(x) - 0.5f, static_cast<float>(size.y) - 0.5f, floor}
        );
    for (int y = 0; y <= size.y; ++y)
        line(
            {-0.5f, static_cast<float>(y) - 0.5f, floor},
            {static_cast<float>(size.x) - 0.5f, static_cast<float>(y) - 0.5f, floor}
        );
}

struct ConstructionPlane {
    IRMath::vec3 origin_;
    IRMath::ivec3 size_;

    IRMath::vec3 center() const {
        return origin_ + IRMath::vec3(size_.x * 0.5f, size_.y * 0.5f, size_.z) - 0.5f;
    }
};

inline std::optional<ConstructionPlane> constructionPlane(IREntity::EntityId target) {
    if (target == IREntity::kNullEntity || !isEditable(target))
        return std::nullopt;
    const auto set = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(target);
    if (!set || (*set)->globalPositions_.empty())
        return std::nullopt;
    return ConstructionPlane{
        IRMath::vec3(IRMath::roundVec3HalfUp((*set)->globalPositions_[0].pos_)),
        (*set)->size_
    };
}

inline void appendConstructionGrid(
    IREntity::EntityId target, bool enabled, std::vector<IRPrefab::WorldPrimitiveVertex> &lines
) {
    if (enabled) {
        if (const auto plane = constructionPlane(target))
            appendConstructionGridLines(
                plane->origin_,
                plane->size_,
                lines,
                IRRender::isSpatialViewEnabled()
                    ? IRMath::normalize(IRRender::unprojectView(IRMath::vec2(0), 3.0f)).z
                    : 1.0f
            );
    }
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_CONSTRUCTION_GRID_H */

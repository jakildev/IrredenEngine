#ifndef IR_VOXEL_EDITOR_CONSTRUCTION_GRID_H
#define IR_VOXEL_EDITOR_CONSTRUCTION_GRID_H

#include "editor_picking.hpp"

#include <irreden/render/depth_tested_primitives.hpp>

namespace IRVoxelEditor {

// The +Z-down support plane is the bottom face of the last editable layer.
// XY grid boundaries likewise lie half a cell from integer voxel centers.
inline void appendConstructionGridLines(
    IRMath::vec3 origin, IRMath::ivec3 size, std::vector<IRPrefab::WorldPrimitiveVertex> &lines
) {
    const float floor = static_cast<float>(size.z) - 0.5f;
    const auto line = [&](IRMath::vec3 from, IRMath::vec3 to) {
        const IRMath::vec4 color(0.28f, 0.38f, 0.46f, 0.6f);
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

inline void appendConstructionGrid(
    IREntity::EntityId target, bool enabled, std::vector<IRPrefab::WorldPrimitiveVertex> &lines
) {
    if (!enabled || target == IREntity::kNullEntity || !isEditable(target))
        return;
    const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(target);
    if (set.globalPositions_.empty())
        return;
    appendConstructionGridLines(
        IRMath::vec3(IRMath::roundVec3HalfUp(set.globalPositions_[0].pos_)),
        set.size_,
        lines
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_CONSTRUCTION_GRID_H */

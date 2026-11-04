#ifndef IR_VOXEL_EDITOR_SCULPT_GRID_H
#define IR_VOXEL_EDITOR_SCULPT_GRID_H

#include <irreden/ir_math.hpp>

#include <optional>
#include <cstdint>

namespace IRVoxelEditor {

inline bool cellInBounds(IRMath::ivec3 cell, IRMath::ivec3 size) {
    return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 && cell.x < size.x && cell.y < size.y &&
           cell.z < size.z;
}

// Coordinates are local to the voxel set. A stroke keeps the press plane so
// its own newly placed voxels cannot pull subsequent samples toward the camera.
inline std::optional<IRMath::ivec3> pickSculptPlane(
    IRMath::vec3 rayOrigin,
    IRMath::vec3 rayDirection,
    IRMath::ivec3 size,
    int axis,
    int slice,
    float planeOffset = 0.0f
) {
    if (axis < 0 || axis > 2 || slice < 0 || slice >= size[axis] ||
        IRMath::abs(rayDirection[axis]) < 1e-6f) {
        return std::nullopt;
    }
    const float distance =
        (static_cast<float>(slice) + planeOffset - rayOrigin[axis]) / rayDirection[axis];
    IRMath::ivec3 cell = IRMath::roundVec3HalfUp(rayOrigin + distance * rayDirection);
    cell[axis] = slice;
    return cellInBounds(cell, size) ? std::optional{cell} : std::nullopt;
}

// Includes both endpoints and visits every intermediate cell even when the
// pointer crosses several cells between input frames.
template <typename Visit>
void forEachStrokeCell(IRMath::ivec3 from, IRMath::ivec3 to, Visit &&visit) {
    const IRMath::ivec3 delta = to - from;
    const int steps =
        IRMath::max(IRMath::abs(delta.x), IRMath::max(IRMath::abs(delta.y), IRMath::abs(delta.z)));
    if (steps == 0) {
        visit(from);
        return;
    }
    for (int step = 0; step <= steps; ++step) {
        IRMath::ivec3 cell{};
        for (int axis = 0; axis < 3; ++axis) {
            const std::int64_t numerator = static_cast<std::int64_t>(from[axis]) * steps +
                                           static_cast<std::int64_t>(delta[axis]) * step;
            std::int64_t whole = numerator / steps;
            std::int64_t remainder = numerator % steps;
            if (remainder < 0) {
                --whole;
                remainder += steps;
            }
            cell[axis] = static_cast<int>(whole + (2 * remainder >= steps ? 1 : 0));
        }
        visit(cell);
    }
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_SCULPT_GRID_H */

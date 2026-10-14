#ifndef IR_VOXEL_EDITOR_FILL_GHOST_H
#define IR_VOXEL_EDITOR_FILL_GHOST_H

#include <irreden/ir_math.hpp>

// Geometry of the fill tool's drag preview: the BOX shape that stands for the
// cells a left drag will fill or carve on release.
namespace IRVoxelEditor {

struct FillGhostBox {
    IRMath::vec3 center_ = IRMath::vec3(0.0f);
    IRMath::vec4 params_ = IRMath::vec4(0.0f);
};

// The BOX covering exactly the cells of the inclusive range lo..hi. A BOX's
// params are voxel counts, and a cell is centred on its integer coordinate, so
// the middle of an even count of cells is a half coordinate.
inline FillGhostBox fillGhostBox(IRMath::ivec3 lo, IRMath::ivec3 hi) {
    return FillGhostBox{
        (IRMath::vec3(lo) + IRMath::vec3(hi)) * 0.5f,
        IRMath::vec4(IRMath::vec3(hi - lo + IRMath::ivec3(1)), 0.0f)
    };
}

// Whether the cardinal camera's view ray through @p point, which runs along
// (1,1,1), crosses the box of cells lo..hi, a cell c spanning c - 0.5 to
// c + 0.5 on each axis.
inline bool viewRayCrossesCells(IRMath::vec3 point, IRMath::ivec3 lo, IRMath::ivec3 hi) {
    const IRMath::vec3 enter = IRMath::vec3(lo) - IRMath::vec3(0.5f) - point;
    const IRMath::vec3 leave = IRMath::vec3(hi) + IRMath::vec3(0.5f) - point;
    return IRMath::max(enter.x, IRMath::max(enter.y, enter.z)) <=
           IRMath::min(leave.x, IRMath::min(leave.y, leave.z));
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_FILL_GHOST_H */

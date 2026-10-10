#ifndef IR_VOXEL_EDITOR_LOFT_PANEL_H
#define IR_VOXEL_EDITOR_LOFT_PANEL_H

#include <irreden/ir_math.hpp>

// The loft tool's two mask grids on the GUI canvas: XZ (front) and YZ (side),
// one cell per voxel of the single-set scene. Shared by the editor, which
// draws and hit-tests them, and by sessions that click a mask cell (see
// anim_panel.hpp for why the geometry is shared).

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kLoftGridXZPos{4, 30};
constexpr IRMath::ivec2 kLoftGridYZPos{76, 30};
constexpr int kLoftCellPx = 4;

// Centre of mask cell @p cell (h, v) in a grid drawn at @p gridPos with
// @p gridRows rows. Row 0 is the bottom row on the canvas, the convention
// IRRender::hitTestGridCell returns.
constexpr IRMath::vec2
loftCellCenterGuiTrixel(IRMath::ivec2 gridPos, IRMath::ivec2 cell, int gridRows) {
    return IRMath::vec2(
        static_cast<float>(gridPos.x + cell.x * kLoftCellPx) +
            static_cast<float>(kLoftCellPx) / 2.0f,
        static_cast<float>(gridPos.y + (gridRows - 1 - cell.y) * kLoftCellPx) +
            static_cast<float>(kLoftCellPx) / 2.0f
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_LOFT_PANEL_H */

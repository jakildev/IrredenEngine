#ifndef IR_VOXEL_EDITOR_SCULPT_PANEL_H
#define IR_VOXEL_EDITOR_SCULPT_PANEL_H

#include <irreden/ir_math.hpp>

#include <array>
#include <optional>

namespace IRVoxelEditor {

enum class SculptTool { BRUSH, PAINT, BOX, LINE, BUCKET, PICKER };

struct SculptToolbarButton {
    const char *label_;
    const char *help_;
    int slot_;
    std::optional<SculptTool> tool_;
};

// Array indices are stable gesture IDs. Slots group tools visually without
// changing the IDs used by editor sessions.
inline constexpr std::array<SculptToolbarButton, 12> kSculptButtons{{
    {"BRUSH",
     "Brush: click the floor or a face to add a voxel. Drag for a continuous stroke.",
     0,
     SculptTool::BRUSH},
    {"ERASE", "Erase: drag over occupied voxels to remove them.", 1, SculptTool::BRUSH},
    {"RECOLOR",
     "Recolor: drag over occupied voxels to apply the active palette color.",
     2,
     SculptTool::PAINT},
    {"BOX", "Box: drag between two cells to fill a box.", 5, SculptTool::BOX},
    {"LINE", "Line: drag between two cells to draw a line.", 6, SculptTool::LINE},
    {"NEW PART", "New Part: add a voxel part with its own editable grid.", 7, std::nullopt},
    {"UNDO", "Undo the last edit.", 8, std::nullopt},
    {"REDO", "Redo the last undone edit.", 9, std::nullopt},
    {"GRID", "Show or hide the construction grid.", 10, std::nullopt},
    {"BUCKET",
     "Bucket: click an enclosed empty area to fill it on the visible grid plane.",
     3,
     SculptTool::BUCKET},
    {"PICKER",
     "Picker: click a visible voxel to sample its RGB, then recolor with that color.",
     4,
     SculptTool::PICKER},
    {"SHAPES",
     "Shapes: choose a primitive and voxel dimensions, then spawn a new editable part.",
     11,
     std::nullopt},
}};

constexpr IRMath::ivec2 kSculptToolbarPos{4, 60};
constexpr IRMath::ivec2 kSculptButtonSize{55, 18};
constexpr int kSculptButtonStride = 58;
constexpr IRMath::ivec2 kSculptToolbarSize{
    10 * kSculptButtonStride + kSculptButtonSize.x, kSculptButtonSize.y
};

constexpr IRMath::ivec2 sculptButtonPos(int index) {
    if (index == 11)
        return {1072, 60};
    return kSculptToolbarPos + IRMath::ivec2(kSculptButtons[index].slot_ * kSculptButtonStride, 0);
}

constexpr IRMath::vec2 sculptButtonCenter(int index) {
    return IRMath::vec2(sculptButtonPos(index)) + IRMath::vec2(kSculptButtonSize) * 0.5f;
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_SCULPT_PANEL_H */

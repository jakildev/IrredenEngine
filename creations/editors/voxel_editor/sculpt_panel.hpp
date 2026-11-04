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
    int left_;
    int width_;
    std::optional<SculptTool> tool_;
};

// Array indices are stable gesture IDs used by editor sessions.
inline constexpr std::array<SculptToolbarButton, 12> kSculptButtons{{
    {"BRUSH", "Brush: drag across the floor or a face to add voxels.", 4, 55, SculptTool::BRUSH},
    {"ERASE", "Erase: drag over voxels to remove them.", 62, 55, SculptTool::BRUSH},
    {"RECOLOR",
     "Recolor: drag over occupied voxels with the active color.",
     120,
     55,
     SculptTool::PAINT},
    {"FILL BOX IN CURRENT PART",
     "Box fill: drag between two cells to fill the current part.",
     662,
     200,
     SculptTool::BOX},
    {"LINE",
     "Line: drag between two cells. Shift+drag up/down to draw vertically.",
     383,
     45,
     SculptTool::LINE},
    {"NEW PART",
     "New Part: create an empty part using the New Grid Size dimensions.",
     431,
     59,
     std::nullopt},
    {"UNDO", "Undo the last edit.", 493, 45, std::nullopt},
    {"REDO", "Redo the last undone edit.", 541, 45, std::nullopt},
    {"GRID", "Show or hide the construction grid.", 589, 48, std::nullopt},
    {"BUCKET",
     "Bucket: click inside a closed outline to fill it on one plane.",
     178,
     55,
     SculptTool::BUCKET},
    {"COLOR PICKER",
     "Color Picker: sample a visible voxel, then recolor with its RGB.",
     236,
     85,
     SculptTool::PICKER},
    {"SHAPES",
     "Shapes: stamp a voxel part, drag its dimensions, or box-fill the current part.",
     324,
     56,
     std::nullopt},
}};
constexpr IRMath::ivec2 kSculptToolbarPos{4, 60};
constexpr IRMath::ivec2 kSculptToolbarSize{633, 18};
constexpr IRMath::ivec2 kBoxFillButtonPos{662, 456};
constexpr IRMath::ivec2 sculptButtonPos(int index) {
    return index == 3 ? kBoxFillButtonPos
                      : IRMath::ivec2(kSculptButtons[index].left_, kSculptToolbarPos.y);
}
constexpr IRMath::ivec2 sculptButtonSize(int index) {
    return {kSculptButtons[index].width_, 18};
}
constexpr IRMath::vec2 sculptButtonCenter(int index) {
    return IRMath::vec2(sculptButtonPos(index)) + IRMath::vec2(sculptButtonSize(index)) * 0.5f;
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_SCULPT_PANEL_H */

#ifndef IR_VOXEL_EDITOR_COMPONENTS_PANEL_H
#define IR_VOXEL_EDITOR_COMPONENTS_PANEL_H

#include <irreden/ir_math.hpp>

#include "lod_panel.hpp"

// GUI-canvas geometry of the COMPONENTS panel: the attachable-component list,
// the ROOT target toggle, ATTACH / DETACH, a status line, and one row per
// field of the selected attached component. Shared by the editor, which builds
// the widgets from it, and the component_attach session, which aims at them.
//
// The panel sits under LOD, in the column right of RECIPES, so it overlaps no
// panel a session drives.

namespace IRVoxelEditor {

// The panel builds this many field rows; further fields are listed as a count
// and are not editable here.
constexpr int kMaxComponentFieldRows = 5;

constexpr IRMath::ivec2 kComponentsPanelPos{kLodPanelPos.x, kLodPanelPos.y + kLodPanelSize.y + 6};
constexpr IRMath::ivec2 kComponentsPanelSize{170, 206};

constexpr IRMath::ivec2 kComponentListPos{kComponentsPanelPos.x + 4, kComponentsPanelPos.y + 18};
constexpr IRMath::ivec2 kComponentListSize{162, 52};
constexpr int kComponentListItemHeight = 13;

constexpr int kComponentControlsY = kComponentsPanelPos.y + 74;
constexpr IRMath::ivec2 kComponentRootTogglePos{kComponentsPanelPos.x + 4, kComponentControlsY};
constexpr IRMath::ivec2 kComponentRootToggleSize{54, 14};
constexpr IRMath::ivec2 kComponentAttachPos{kComponentsPanelPos.x + 64, kComponentControlsY};
constexpr IRMath::ivec2 kComponentDetachPos{kComponentsPanelPos.x + 116, kComponentControlsY};
constexpr IRMath::ivec2 kComponentButtonSize{50, 14};

constexpr IRMath::ivec2 kComponentStatusPos{kComponentsPanelPos.x + 4, kComponentControlsY + 18};

constexpr int kComponentFieldTop = kComponentControlsY + 36;
constexpr int kComponentFieldPitch = 18;
constexpr int kComponentFieldLabelX = kComponentsPanelPos.x + 4;
constexpr int kComponentFieldInputX = kComponentsPanelPos.x + 70;
constexpr IRMath::ivec2 kComponentFieldInputSize{96, 14};
// An ENGINE component's overrides span the row under its label.
constexpr IRMath::ivec2 kComponentOverridesInputSize{162, 14};

constexpr int componentFieldRowY(int row) {
    return kComponentFieldTop + row * kComponentFieldPitch;
}

constexpr IRMath::vec2 componentListRowCenterGuiTrixel(int row) {
    return IRMath::vec2(
        static_cast<float>(kComponentListPos.x) + static_cast<float>(kComponentListSize.x) / 2.0f,
        static_cast<float>(kComponentListPos.y) +
            (static_cast<float>(row) + 0.5f) * static_cast<float>(kComponentListItemHeight)
    );
}

constexpr IRMath::vec2 componentButtonCenterGuiTrixel(IRMath::ivec2 pos) {
    return IRMath::vec2(
        static_cast<float>(pos.x) + static_cast<float>(kComponentButtonSize.x) / 2.0f,
        static_cast<float>(pos.y) + static_cast<float>(kComponentButtonSize.y) / 2.0f
    );
}

constexpr IRMath::vec2 componentFieldInputCenterGuiTrixel(int row) {
    return IRMath::vec2(
        static_cast<float>(kComponentFieldInputX) +
            static_cast<float>(kComponentFieldInputSize.x) / 2.0f,
        static_cast<float>(componentFieldRowY(row)) +
            static_cast<float>(kComponentFieldInputSize.y) / 2.0f
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_COMPONENTS_PANEL_H */

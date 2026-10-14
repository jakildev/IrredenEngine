#ifndef IR_VOXEL_EDITOR_PARTS_PANEL_H
#define IR_VOXEL_EDITOR_PARTS_PANEL_H

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/components/component_widget.hpp>

// GUI-canvas geometry of the PARTS panel, list, mode dropdown, and remove
// button. Shared by the editor, which builds the widgets from it, and scripted
// sessions, which aim clicks at its controls. Row maths comes from
// C_WidgetList and C_WidgetDropdown, so an aim lands on the row the widget
// resolves it to.

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kPartsPanelPos{254, 240};
constexpr IRMath::ivec2 kPartsPanelSize{120, 116};

constexpr IRMath::ivec2 kPartsListPos{kPartsPanelPos.x + 4, kPartsPanelPos.y + 18};
constexpr IRMath::ivec2 kPartsListSize{112, 44};
constexpr int kPartsListItemHeight = 13;
constexpr IRMath::ivec2 kPartModeDropdownPos{kPartsPanelPos.x + 4, kPartsPanelPos.y + 66};
constexpr IRMath::ivec2 kPartModeDropdownSize{112, 18};
constexpr int kPartModeDropdownItemHeight = 16;
constexpr IRMath::ivec2 kPartRemoveButtonPos{kPartsPanelPos.x + 4, kPartsPanelPos.y + 88};
constexpr IRMath::ivec2 kPartRemoveButtonSize{112, 18};

// The mode dropdown's rows, in the order its items are listed.
constexpr int partModeRow(IRComponents::RotationMode mode) {
    switch (mode) {
    case IRComponents::RotationMode::GRID:
        return 0;
    case IRComponents::RotationMode::DETACHED:
        return 1;
    case IRComponents::RotationMode::DETACHED_REVOXELIZE:
        return 2;
    }
    return 0;
}

constexpr IRComponents::RotationMode partModeFromRow(int row) {
    switch (row) {
    case 1:
        return IRComponents::RotationMode::DETACHED;
    case 2:
        return IRComponents::RotationMode::DETACHED_REVOXELIZE;
    default:
        return IRComponents::RotationMode::GRID;
    }
}

constexpr IRMath::vec2 partModeDropdownHeaderCenterGuiTrixel() {
    return IRMath::vec2(
        static_cast<float>(kPartModeDropdownPos.x) +
            static_cast<float>(kPartModeDropdownSize.x) / 2.0f,
        static_cast<float>(kPartModeDropdownPos.y) +
            static_cast<float>(kPartModeDropdownSize.y) / 2.0f
    );
}

// Centre of @p mode's row in the expanded dropdown.
inline IRMath::vec2 partModeDropdownRowCenterGuiTrixel(IRComponents::RotationMode mode) {
    IRComponents::C_WidgetDropdown dropdown;
    dropdown.itemHeight_ = kPartModeDropdownItemHeight;
    return IRMath::vec2(
        static_cast<float>(kPartModeDropdownPos.x) +
            static_cast<float>(kPartModeDropdownSize.x) / 2.0f,
        static_cast<float>(kPartModeDropdownPos.y) +
            dropdown.itemCenterOffsetY(kPartModeDropdownSize.y, partModeRow(mode))
    );
}

constexpr IRMath::vec2 partRemoveButtonCenterGuiTrixel() {
    return IRMath::vec2(
        static_cast<float>(kPartRemoveButtonPos.x) +
            static_cast<float>(kPartRemoveButtonSize.x) / 2.0f,
        static_cast<float>(kPartRemoveButtonPos.y) +
            static_cast<float>(kPartRemoveButtonSize.y) / 2.0f
    );
}

// The list's row geometry with no items in it.
inline IRComponents::C_WidgetList partsListGeometry() {
    IRComponents::C_WidgetList list;
    list.itemHeight_ = kPartsListItemHeight;
    return list;
}

inline int partsListVisibleRows() {
    return partsListGeometry().visibleRows(kPartsListSize.y);
}

// Centre of the list's @p visibleRow-th row from the top, whichever part the
// scroll offset currently puts there.
inline IRMath::vec2 partsListRowCenterGuiTrixel(int visibleRow) {
    return IRMath::vec2(
        static_cast<float>(kPartsListPos.x) + static_cast<float>(kPartsListSize.x) / 2.0f,
        static_cast<float>(kPartsListPos.y) + partsListGeometry().rowCenterOffsetY(visibleRow)
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_PARTS_PANEL_H */

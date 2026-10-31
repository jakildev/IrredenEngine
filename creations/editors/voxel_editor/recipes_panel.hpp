#ifndef IR_VOXEL_EDITOR_RECIPES_PANEL_H
#define IR_VOXEL_EDITOR_RECIPES_PANEL_H

#include <irreden/ir_math.hpp>
#include <irreden/render/components/component_widget.hpp>

#include "anim_panel.hpp"

// GUI-canvas geometry of the module UI: the RECIPES panel (recipe list, one
// slider per parameter, APPLY) and the column of module panels docked under
// it. Shared by the editor, which builds the widgets from it, and the
// module_loaded session, which aims scripted clicks and drags at them — the
// same split palette.hpp and anim_panel.hpp make.
//
// The column begins below ARRAY and right of COMPONENTS.

namespace IRVoxelEditor {

constexpr int kRecipeParamRowsPerPage = 3;
constexpr int kMaxRecipeParams = 16;

constexpr IRMath::ivec2 kRecipesPanelPos{682, 240};
constexpr IRMath::ivec2 kRecipesPanelSize{120, 150};

constexpr IRMath::ivec2 kRecipeListPos{kRecipesPanelPos.x + 4, kRecipesPanelPos.y + 18};
constexpr IRMath::ivec2 kRecipeListSize{112, 52};
constexpr int kRecipeListItemHeight = 13;

constexpr int kRecipeSliderTop = kRecipesPanelPos.y + 74;
constexpr int kRecipeSliderPitch = 18;
constexpr IRMath::ivec2 kRecipeSliderSize{112, 14};

constexpr int kRecipeControlsY = kRecipeSliderTop + kRecipeParamRowsPerPage * kRecipeSliderPitch;
constexpr IRMath::ivec2 kRecipeApplyPos{kRecipesPanelPos.x + 4, kRecipeControlsY};
constexpr IRMath::ivec2 kRecipeApplySize{40, 14};
constexpr IRMath::ivec2 kRecipePagePrevPos{kRecipeApplyPos.x + 42, kRecipeControlsY};
constexpr IRMath::ivec2 kRecipePageLabelPos{kRecipeApplyPos.x + 66, kRecipeControlsY};
constexpr IRMath::ivec2 kRecipePageNextPos{kRecipeApplyPos.x + 90, kRecipeControlsY};
constexpr IRMath::ivec2 kRecipePageButtonSize{22, 14};

constexpr int recipeParamPageCount(int paramCount) {
    return IRMath::max(1, IRMath::divCeil(paramCount, kRecipeParamRowsPerPage));
}

constexpr IRMath::ivec2 kModulePanelSize{120, 48};
constexpr int kModulePanelGap = 6;

constexpr SliderGeometry recipeParamSliderGeometry(int paramIndex) {
    return SliderGeometry{
        IRMath::ivec2(kRecipesPanelPos.x + 4, kRecipeSliderTop + paramIndex * kRecipeSliderPitch),
        kRecipeSliderSize
    };
}

// Top-left of the @p index-th module panel, stacked below RECIPES.
constexpr IRMath::ivec2 modulePanelPos(int index) {
    return IRMath::ivec2(
        kRecipesPanelPos.x,
        kRecipesPanelPos.y + kRecipesPanelSize.y + kModulePanelGap +
            index * (kModulePanelSize.y + kModulePanelGap)
    );
}

// Whether @p guiTrixel lies in the half-open box [pos, pos + size), the
// extent a GUI hitbox covers.
constexpr bool guiPointInRect(IRMath::vec2 guiTrixel, IRMath::ivec2 pos, IRMath::ivec2 size) {
    return guiTrixel.x >= static_cast<float>(pos.x) && guiTrixel.y >= static_cast<float>(pos.y) &&
           guiTrixel.x < static_cast<float>(pos.x + size.x) &&
           guiTrixel.y < static_cast<float>(pos.y + size.y);
}

// The list's row geometry with no items in it.
inline IRComponents::C_WidgetList recipeListGeometry() {
    IRComponents::C_WidgetList list;
    list.itemHeight_ = kRecipeListItemHeight;
    return list;
}

inline int recipeListVisibleRows() {
    return recipeListGeometry().visibleRows(kRecipeListSize.y);
}

// Centre of the list's @p visibleRow-th row from the top, whichever recipe the
// scroll offset currently puts there.
inline IRMath::vec2 recipeListRowCenterGuiTrixel(int visibleRow) {
    return IRMath::vec2(
        static_cast<float>(kRecipeListPos.x) + static_cast<float>(kRecipeListSize.x) / 2.0f,
        static_cast<float>(kRecipeListPos.y) + recipeListGeometry().rowCenterOffsetY(visibleRow)
    );
}

constexpr IRMath::vec2 recipeApplyCenterGuiTrixel() {
    return IRMath::vec2(
        static_cast<float>(kRecipeApplyPos.x) + static_cast<float>(kRecipeApplySize.x) / 2.0f,
        static_cast<float>(kRecipeApplyPos.y) + static_cast<float>(kRecipeApplySize.y) / 2.0f
    );
}

constexpr IRMath::vec2 recipePageButtonCenterGuiTrixel(IRMath::ivec2 pos) {
    return IRMath::vec2(
        static_cast<float>(pos.x) + static_cast<float>(kRecipePageButtonSize.x) / 2.0f,
        static_cast<float>(pos.y) + static_cast<float>(kRecipePageButtonSize.y) / 2.0f
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_RECIPES_PANEL_H */

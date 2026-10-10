#ifndef IR_VOXEL_EDITOR_RECIPES_PANEL_H
#define IR_VOXEL_EDITOR_RECIPES_PANEL_H

#include <irreden/ir_math.hpp>

#include "anim_panel.hpp"

// GUI-canvas geometry of the module UI: the RECIPES panel (recipe list, one
// slider per parameter, APPLY) and the column of module panels docked under
// it. Shared by the editor, which builds the widgets from it, and the
// module_loaded session, which aims scripted clicks and drags at them — the
// same split palette.hpp and anim_panel.hpp make.
//
// The column sits below ARRAY and right of COMPONENTS.

namespace IRVoxelEditor {

// The panel builds this many parameter sliders; a recipe declaring more is a
// load error.
constexpr int kMaxRecipeParams = 3;

constexpr IRMath::ivec2 kRecipesPanelPos{682, 240};
constexpr IRMath::ivec2 kRecipesPanelSize{120, 150};

constexpr IRMath::ivec2 kRecipeListPos{kRecipesPanelPos.x + 4, kRecipesPanelPos.y + 18};
constexpr IRMath::ivec2 kRecipeListSize{112, 52};
constexpr int kRecipeListItemHeight = 13;

constexpr int kRecipeSliderTop = kRecipesPanelPos.y + 74;
constexpr int kRecipeSliderPitch = 18;
constexpr IRMath::ivec2 kRecipeSliderSize{112, 14};

constexpr IRMath::ivec2 kRecipeApplyPos{
    kRecipesPanelPos.x + 4, kRecipeSliderTop + kMaxRecipeParams *kRecipeSliderPitch
};
constexpr IRMath::ivec2 kRecipeApplySize{112, 14};

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

// Centre of recipe-list row @p row — where WIDGET_APPLY_LIST resolves a click
// to that row ((mouseY - pos.y) / itemHeight).
constexpr IRMath::vec2 recipeListRowCenterGuiTrixel(int row) {
    return IRMath::vec2(
        static_cast<float>(kRecipeListPos.x) + static_cast<float>(kRecipeListSize.x) / 2.0f,
        static_cast<float>(kRecipeListPos.y) +
            (static_cast<float>(row) + 0.5f) * static_cast<float>(kRecipeListItemHeight)
    );
}

constexpr IRMath::vec2 recipeApplyCenterGuiTrixel() {
    return IRMath::vec2(
        static_cast<float>(kRecipeApplyPos.x) + static_cast<float>(kRecipeApplySize.x) / 2.0f,
        static_cast<float>(kRecipeApplyPos.y) + static_cast<float>(kRecipeApplySize.y) / 2.0f
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_RECIPES_PANEL_H */

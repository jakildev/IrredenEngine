#ifndef IR_VOXEL_EDITOR_ARRAY_PANEL_H
#define IR_VOXEL_EDITOR_ARRAY_PANEL_H

#include "anim_panel.hpp"

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kArrayPanelPos{682, 20};
constexpr IRMath::ivec2 kArrayPanelSize{130, 174};
constexpr int kArrayMaxCount = 12;
constexpr SliderGeometry kArrayCountSliderGeometry{
    IRMath::ivec2(kArrayPanelPos.x + 4, kArrayPanelPos.y + 50), IRMath::ivec2(122, 14)
};
constexpr IRMath::vec2 kArrayApplyCenter{
    static_cast<float>(kArrayPanelPos.x + 65), static_cast<float>(kArrayPanelPos.y + 149)
};

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_ARRAY_PANEL_H */

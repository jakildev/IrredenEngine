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
// COPY YAW: the whole degrees each radial copy is turned about the array axis
// on top of the copy before it. A drag lands on a whole trixel and the track
// is 120 trixels across 360 degrees, so the reachable turns are the multiples
// of 3.
constexpr float kArrayYawSliderMin = -180.0f;
constexpr float kArrayYawSliderMax = 180.0f;
constexpr SliderGeometry kArrayYawSliderGeometry{
    IRMath::ivec2(kArrayPanelPos.x + 4, kArrayPanelPos.y + 122), IRMath::ivec2(120, 14)
};
static_assert(
    (kArrayYawSliderMax - kArrayYawSliderMin) /
            static_cast<float>(kArrayYawSliderGeometry.size_.x) ==
        3.0f,
    "COPY YAW's track must be 3 degrees a trixel"
);
constexpr IRMath::vec2 kArrayApplyCenter{
    static_cast<float>(kArrayPanelPos.x + 65), static_cast<float>(kArrayPanelPos.y + 149)
};

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_ARRAY_PANEL_H */

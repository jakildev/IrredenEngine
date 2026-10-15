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
constexpr float kArrayRadiusSliderMin = -12.0f;
constexpr float kArrayRadiusSliderMax = 12.0f;
constexpr int kArrayDefaultRadius = 4;
constexpr SliderGeometry kArrayRadiusSliderGeometry{
    IRMath::ivec2(kArrayPanelPos.x + 4, kArrayPanelPos.y + 68), IRMath::ivec2(122, 14)
};
static_assert(
    static_cast<float>(kArrayRadiusSliderGeometry.size_.x) /
            (kArrayRadiusSliderMax - kArrayRadiusSliderMin) >=
        4.0f,
    "RADIUS/X needs at least four trixels per whole unit"
);
// COPY YAW: the whole degrees each radial copy is turned about the array axis
// on top of the copy before it. A drag lands on a whole trixel and the track
// is 120 trixels across 360 degrees, so the reachable turns are the multiples
// of 3.
constexpr float kArrayYawSliderMin = -180.0f;
constexpr float kArrayYawSliderMax = 180.0f;
constexpr SliderGeometry kArrayYawSliderGeometry{
    IRMath::ivec2(kArrayPanelPos.x + 4, kArrayPanelPos.y + 122), IRMath::ivec2(120, 14)
};
constexpr float kArrayYawDegreesPerTrixel =
    (kArrayYawSliderMax - kArrayYawSliderMin) / static_cast<float>(kArrayYawSliderGeometry.size_.x);
static_assert(kArrayYawDegreesPerTrixel == 3.0f, "COPY YAW's track must be 3 degrees a trixel");
constexpr IRMath::vec2 kArrayApplyCenter{
    static_cast<float>(kArrayPanelPos.x + 65), static_cast<float>(kArrayPanelPos.y + 149)
};

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_ARRAY_PANEL_H */

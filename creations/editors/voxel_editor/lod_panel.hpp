#ifndef IR_VOXEL_EDITOR_LOD_PANEL_H
#define IR_VOXEL_EDITOR_LOD_PANEL_H

#include <irreden/ir_math.hpp>
#include <irreden/render/lod_level.hpp>

#include "anim_panel.hpp"

// The LOD panel: the selected part's inclusive band (FINE, COARSE) and the
// tier scrubber (TIER plus FOLLOW ZOOM). Shared by the editor, which builds the
// widgets, and by sessions that drag a scripted cursor along these tracks (see
// anim_panel.hpp for why the geometry is shared).

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kLodPanelPos{506, 240};
constexpr IRMath::ivec2 kLodPanelSize{120, 96};

// Tier sliders span the engine's tier numbering, finest .. coarsest.
constexpr float kLodTierSliderMin = static_cast<float>(IRRender::LodLevel::LOD_0);
constexpr float kLodTierSliderMax = static_cast<float>(IRRender::LodLevel::LOD_4);

constexpr SliderGeometry kLodFineSliderGeometry{
    IRMath::ivec2(kLodPanelPos.x + 4, kLodPanelPos.y + 20), IRMath::ivec2(112, 14)
};
constexpr SliderGeometry kLodCoarseSliderGeometry{
    IRMath::ivec2(kLodPanelPos.x + 4, kLodPanelPos.y + 38), IRMath::ivec2(112, 14)
};
constexpr SliderGeometry kLodTierSliderGeometry{
    IRMath::ivec2(kLodPanelPos.x + 4, kLodPanelPos.y + 56), IRMath::ivec2(112, 14)
};
constexpr IRMath::ivec2 kLodFollowCheckboxPos{kLodPanelPos.x + 4, kLodPanelPos.y + 76};
constexpr IRMath::ivec2 kLodFollowCheckboxSize{112, 14};

constexpr IRMath::vec2 lodFollowCheckboxCenterGuiTrixel() {
    return IRMath::vec2(
        static_cast<float>(kLodFollowCheckboxPos.x) + 6.0f,
        static_cast<float>(kLodFollowCheckboxPos.y) +
            static_cast<float>(kLodFollowCheckboxSize.y) / 2.0f
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_LOD_PANEL_H */

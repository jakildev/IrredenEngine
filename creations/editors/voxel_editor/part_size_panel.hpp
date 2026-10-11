#ifndef IR_VOXEL_EDITOR_PART_SIZE_PANEL_H
#define IR_VOXEL_EDITOR_PART_SIZE_PANEL_H

#include <irreden/ir_math.hpp>

#include "anim_panel.hpp"

// The PART SIZE panel: the W / H / D extent the next Ctrl+P voxel part is
// created at, and a read-only readout of the selected voxel part's extent.
// Shared by the editor, which builds the widgets, and by sessions that drag a
// scripted cursor along these tracks (see anim_panel.hpp for why the geometry
// is shared).
//
// The sliders are creation parameters: they do not follow the selection and
// never resize an existing part.

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kPartSizePanelPos{4, 598};
constexpr IRMath::ivec2 kPartSizePanelSize{120, 96};

constexpr int kPartSizeAxisCount = 3;
constexpr int kPartSizeSliderPitch = 18;
constexpr IRMath::ivec2 kPartSizeSliderSize{112, 14};
constexpr IRMath::ivec2 kPartSizeReadoutPos{kPartSizePanelPos.x + 4, kPartSizePanelPos.y + 78};

// Track geometry for axis 0 (W, x), 1 (H, y) or 2 (D, z).
constexpr SliderGeometry partSizeSliderGeometry(int axis) {
    return SliderGeometry{
        IRMath::ivec2(
            kPartSizePanelPos.x + 4,
            kPartSizePanelPos.y + 20 + axis * kPartSizeSliderPitch
        ),
        kPartSizeSliderSize
    };
}

// A track spans 1 .. max(kPartSizeSliderFloorMax, the scene's own extent on
// that axis), so a --scene-size larger than the floor is still reachable.
constexpr float kPartSizeSliderMin = 1.0f;
constexpr int kPartSizeSliderFloorMax = 32;

constexpr float partSizeSliderMax(int sceneAxisSize) {
    return static_cast<float>(
        sceneAxisSize > kPartSizeSliderFloorMax ? sceneAxisSize : kPartSizeSliderFloorMax
    );
}

// World z of a voxel set's far (local z == size.z - 1) layer: the seed ground
// plane of the single-set scene, and the layer every created part rests on.
constexpr int kSeedGroundPlaneWorldZ = 3;

// Translation that centres a set of @p size in X / Y and seats its far z layer
// at kSeedGroundPlaneWorldZ, so the camera framing and the authoring recipes
// stay height-agnostic. An odd X / Y extent gives a half-integer origin.
inline IRMath::vec3 deriveSceneOrigin(IRMath::ivec3 size) {
    return IRMath::vec3(
        -static_cast<float>(size.x) / 2.0f,
        -static_cast<float>(size.y) / 2.0f,
        static_cast<float>(kSeedGroundPlaneWorldZ - (size.z - 1))
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_PART_SIZE_PANEL_H */

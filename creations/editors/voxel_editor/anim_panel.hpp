#ifndef IR_VOXEL_EDITOR_ANIM_PANEL_H
#define IR_VOXEL_EDITOR_ANIM_PANEL_H

#include <irreden/ir_math.hpp>

// The ANIM panel's two sliders (frame scrubber + FPS): the GUI-canvas
// geometry both the editor and an authoring session build from.
//
// Lives outside initEntities for the same reason palette.hpp's swatch grid
// does — two callers need it: the editor builds the slider widgets from it,
// and a session drags a scripted cursor along a track
// (Session::Builder::dragGuiSlider). A session that hardcoded a screen pixel
// would silently start missing the track the first time the panel moved, so
// both sides derive from one description of the geometry.

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kAnimPanelPos{4, 420};
constexpr IRMath::ivec2 kAnimPanelSize{120, 55};

// Position + size of a slider's track — matches the C_Widget::size_ a
// makeSlider call gives it, the full width system_widget_input.hpp's
// press/drag math maps [0,1] across.
struct SliderGeometry {
    IRMath::ivec2 pos_;
    IRMath::ivec2 size_;
};

constexpr SliderGeometry kScrubberSliderGeometry{
    IRMath::ivec2(kAnimPanelPos.x + 4, kAnimPanelPos.y + 22), IRMath::ivec2(112, 14)
};
constexpr SliderGeometry kFpsSliderGeometry{
    IRMath::ivec2(kAnimPanelPos.x + 4, kAnimPanelPos.y + 38), IRMath::ivec2(112, 14)
};

// The scrubber's range tracks the live animation's frame count
// (frameCount() - 1 — see main.cpp's makeSlider call), so only its floor is a
// compile-time constant. The FPS range is fixed by the widget's own contract.
constexpr float kScrubberSliderMinValue = 0.0f;
constexpr float kFpsSliderMinValue = 1.0f;
constexpr float kFpsSliderMaxValue = 30.0f;

// GUI-canvas trixel a scripted drag must land on to leave `value` on the
// track — the inverse of system_widget_input.hpp's press/drag math
// (dragValue = (mouseX - pos.x) / size.x; currentValue = min + dragValue *
// (max - min)). Vertically centred on the track so the aim clears the
// hitbox regardless of where the thumb renders.
constexpr IRMath::vec2
sliderValueGuiTrixel(const SliderGeometry &geom, float minValue, float maxValue, float value) {
    const float t = IRMath::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
    return IRMath::vec2(
        static_cast<float>(geom.pos_.x) + t * static_cast<float>(geom.size_.x),
        static_cast<float>(geom.pos_.y) + static_cast<float>(geom.size_.y) / 2.0f
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_ANIM_PANEL_H */

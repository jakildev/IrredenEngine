#ifndef SYSTEM_CAMERA_SCROLL_ZOOM_H
#define SYSTEM_CAMERA_SCROLL_ZOOM_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_gui_hover_state.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/input/components/component_mouse_scroll.hpp>

using namespace IRComponents;

namespace IRSystem {

// Mouse wheel / trackpad two-finger scroll → camera zoom step.
// Tick iterates ephemeral C_MouseScroll entities (one per scroll event)
// and accumulates a discrete delta; endTick applies the delta once via
// C_ZoomLevel::zoomIn / zoomOut on the named "camera" entity. Each
// integer step doubles or halves the zoom (clamped to engine limits).
//
// The GUI owns the wheel while the cursor is on a widget: with a
// C_GuiHoverState singleton naming a hovered widget, the frame's events are
// dropped here. That reads the hover WIDGET_INPUT::endTick published, so
// register this system after WIDGET_INPUT; ahead of it, the gate lags a frame.
// A creation without the singleton zooms on every event.
template <> struct System<CAMERA_SCROLL_ZOOM> {
    int scrollDelta_ = 0;
    bool guiOwnsWheel_ = false;
    IREntity::EntityId cameraEntity_ = IREntity::kNullEntity;

    void beginTick() {
        cameraEntity_ = IREntity::getEntity("camera");
        scrollDelta_ = 0;
        guiOwnsWheel_ = IRPrefab::Widget::hoveredWidget() != IREntity::kNullEntity;
    }

    void tick(C_MouseScroll &scroll) {
        if (guiOwnsWheel_)
            return;
        if (scroll.yoffset_ > 0.0) ++scrollDelta_;
        else if (scroll.yoffset_ < 0.0) --scrollDelta_;
    }

    void endTick() {
        if (scrollDelta_ == 0 || cameraEntity_ == IREntity::kNullEntity) return;
        auto opt = IREntity::getComponentOptional<C_ZoomLevel>(cameraEntity_);
        if (!opt.has_value()) return;
        C_ZoomLevel &zoom = **opt;
        for (int i = 0; i < scrollDelta_; ++i) zoom.zoomIn();
        for (int i = 0; i > scrollDelta_; --i) zoom.zoomOut();
    }

    static SystemId create() {
        return registerSystem<CAMERA_SCROLL_ZOOM, C_MouseScroll, AlsoReads<C_GuiHoverState>>(
            "CameraScrollZoom"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_SCROLL_ZOOM_H */

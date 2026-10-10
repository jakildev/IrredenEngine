#ifndef SYSTEM_CAMERA_SCROLL_ZOOM_H
#define SYSTEM_CAMERA_SCROLL_ZOOM_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/input/components/component_mouse_scroll.hpp>

using namespace IRComponents;

namespace IRSystem {

// Mouse wheel / trackpad two-finger scroll → camera zoom step.
// Tick iterates ephemeral C_MouseScroll entities (one per scroll event)
// and accumulates a discrete delta; endTick applies the delta once through
// IRPrefab::Camera::zoomIn / zoomOut, which own what a step means under the
// camera's zoom policy. Each integer step doubles or halves the zoom
// (clamped to engine limits).
template <> struct System<CAMERA_SCROLL_ZOOM> {
    int scrollDelta_ = 0;

    void beginTick() {
        scrollDelta_ = 0;
    }

    void tick(C_MouseScroll &scroll) {
        if (scroll.yoffset_ > 0.0) ++scrollDelta_;
        else if (scroll.yoffset_ < 0.0) --scrollDelta_;
    }

    void endTick() {
        for (int i = 0; i < scrollDelta_; ++i) IRPrefab::Camera::zoomIn();
        for (int i = 0; i > scrollDelta_; --i) IRPrefab::Camera::zoomOut();
    }

    static SystemId create() {
        return registerSystem<CAMERA_SCROLL_ZOOM, C_MouseScroll>("CameraScrollZoom");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_SCROLL_ZOOM_H */

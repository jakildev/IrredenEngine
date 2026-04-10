#ifndef CULL_VIEWPORT_STATE_H
#define CULL_VIEWPORT_STATE_H

#include <irreden/ir_math.hpp>
#include <irreden/render/commands/command_toggle_culling_freeze.hpp>

using namespace IRMath;

namespace IRRender {

// Shared cull viewport state.  Updated once per frame (during the render
// event) so that every render system sees a consistent frozen-or-live
// viewport without maintaining its own snapshot.
//
// The state captures the camera iso position, zoom, and active canvas size
// at the moment culling freezes.  When unfrozen it tracks live values.
// Each system derives per-canvas iso bounds from these via isoViewport().
struct CullViewportState {
    vec2 cameraIso_ = vec2(0.0f);
    vec2 zoom_ = vec2(1.0f);
    ivec2 canvasSize_ = ivec2(0);
    bool frozen_ = false;

    // Convenience: compute the iso-space cull viewport for this state.
    // canvasSize can be overridden for systems that render on a different
    // canvas than the one captured at freeze time.
    IsoBounds2D isoViewport(int margin = 0) const {
        return isoViewportForCanvas(canvasSize_, margin);
    }

    IsoBounds2D isoViewportForCanvas(ivec2 canvas, int margin = 0) const {
        return IRMath::visibleIsoViewport(
            cameraIso_,
            IRMath::trixelOriginOffsetZ1(canvas),
            canvas,
            zoom_,
            margin
        );
    }
};

namespace detail {
inline CullViewportState &cullViewportState() {
    static CullViewportState s;
    return s;
}
} // namespace detail

// Call once per frame at the start of the render event.
// Safe to call multiple times per frame — only the first call in a frozen
// transition captures the snapshot; subsequent calls are no-ops when frozen.
inline void updateCullViewport(
    vec2 liveCameraIso,
    vec2 liveZoom,
    ivec2 liveCanvasSize
) {
    auto &s = detail::cullViewportState();
    bool nowFrozen = IRCommand::isCullingFrozen();
    if (!nowFrozen) {
        s.cameraIso_ = liveCameraIso;
        s.zoom_ = liveZoom;
        s.canvasSize_ = liveCanvasSize;
    } else if (!s.frozen_) {
        s.cameraIso_ = liveCameraIso;
        s.zoom_ = liveZoom;
        s.canvasSize_ = liveCanvasSize;
    }
    s.frozen_ = nowFrozen;
}

inline const CullViewportState &getCullViewport() {
    return detail::cullViewportState();
}

} // namespace IRRender

#endif /* CULL_VIEWPORT_STATE_H */

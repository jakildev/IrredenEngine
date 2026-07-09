#ifndef CULL_VIEWPORT_STATE_H
#define CULL_VIEWPORT_STATE_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRRender {

// Cull-freeze flag: pins the shared cull viewport (below) at its current pose
// so the camera can free-fly while the cull stays put. Lives here — next to
// the state it gates — rather than in the toggle-command header, so lower
// layers that drive the cull (the engine/video auto-screenshot harness, #1438)
// can flip it without depending on the command module. The interactive
// IRCommand::Command<TOGGLE_CULLING_FREEZE> and the programmatic setter below
// both write this one flag.
namespace detail {
inline bool &cullingFreezeFlag() {
    static bool frozen = false;
    return frozen;
}
} // namespace detail

inline bool isCullingFrozen() {
    return detail::cullingFreezeFlag();
}

inline void setCullingFrozen(bool frozen) {
    detail::cullingFreezeFlag() = frozen;
}

// Culling-minimap visibility flag (#2316, V2). Lives here — next to the
// cull-freeze flag it's toggled alongside — rather than on
// `System<DEBUG_CULLING_MINIMAP>` itself, so the interactive
// `IRCommand::Command<TOGGLE_CULLING_MINIMAP>` (which has no `SystemId`
// handle at bind time) can flip it the same way `TOGGLE_CULLING_FREEZE`
// flips `cullingFreezeFlag()` above. Default true preserves the minimap's
// current always-on behavior for demos that already register it.
namespace detail {
inline bool &cullingMinimapEnabledFlag() {
    static bool enabled = true;
    return enabled;
}
} // namespace detail

inline bool isCullingMinimapEnabled() {
    return detail::cullingMinimapEnabledFlag();
}

inline void setCullingMinimapEnabled(bool enabled) {
    detail::cullingMinimapEnabledFlag() = enabled;
}

// Chunk margin for the CPU-side cull gate in REBUILD_GRID_VOXELS and the GPU
// chunk-visibility mask in VOXEL_TO_TRIXEL_STAGE_1.  Both use this value so
// the CPU skip decision is never tighter than the GPU raster decision:
// if the GPU still renders a chunk's voxels, the CPU must still rebuild them.
constexpr int kCullChunkMargin = 8;

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

// Pinned light/occlusion anchor storage (#2315, V1). `cameraAnchorVoxel()`
// (camera_anchor.hpp) re-derives the world voxel the light volume and
// occlusion grid center on every frame from the LIVE camera pan — it
// deliberately never consulted the cull freeze. Freezing the cull viewport
// (above) without also pinning this anchor left the light window still
// tracking the camera during a freeze, which defeats the freeze's purpose
// for light/shadow domain inspection. `IRRender::detail::frozenAwareCameraAnchorVoxel()`
// (camera_anchor.hpp) is the only writer — it owns the capture-on-transition
// logic; `BUILD_LIGHT_OCCLUSION_GRID` / `COMPUTE_LIGHT_VOLUME` call that
// helper, never this state directly. `getLightAnchorFreeze()` below is the
// read-only diagnostic accessor (mirrors `getCullViewport()`) for consumers
// that only need the pinned value, not the write-path — e.g. a lighting
// demo's DOMAIN-STATE emission hook reporting the light window bounds.
struct LightAnchorFreezeState {
    ivec3 anchor_ = ivec3(0);
    bool frozen_ = false;
};

namespace detail {
inline LightAnchorFreezeState &lightAnchorFreezeState() {
    static LightAnchorFreezeState s;
    return s;
}
} // namespace detail

inline const LightAnchorFreezeState &getLightAnchorFreeze() {
    return detail::lightAnchorFreezeState();
}

namespace detail {
inline CullViewportState &cullViewportState() {
    static CullViewportState s;
    return s;
}
} // namespace detail

// Call once per frame at the start of the render event.
// Safe to call multiple times per frame — only the first call in a frozen
// transition captures the snapshot; subsequent calls are no-ops when frozen.
inline void updateCullViewport(vec2 liveCameraIso, vec2 liveZoom, ivec2 liveCanvasSize) {
    auto &s = detail::cullViewportState();
    bool nowFrozen = isCullingFrozen();
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

#ifndef PER_AXIS_SURFACE_LIGHTING_H
#define PER_AXIS_SURFACE_LIGHTING_H

#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_per_axis_surface_lighting.hpp>

// Per-axis surface lighting: when enabled, an eligible lit pipeline
// (camera-iso per-axis canvas, no debug overlay) shades per-axis voxel faces
// per output fragment at the continuous point on the face, the receiver the
// cardinal surface route uses, so a sun-shadow edge on a voxel receiver keeps
// its shape as the camera leaves or reaches a cardinal yaw. Disabled, a
// rotating frame takes one query per whole face at its centre. Work scales
// with output fragments and shadow candidates. Read every frame, so it can
// change at runtime.
//
// The same switch admits both surface routes, cardinal and per-axis, on a
// pipeline that registers FOG_TO_TRIXEL, where the composite then fogs each
// surface-lit sample; disabled, fog keeps lighting in compute there. See
// `surfaceFogRoute`. Unset, the switch is on for every pipeline
// (`enabledFor`); `setEnabled` overrides it.
namespace IRPrefab::PerAxisSurfaceLighting {

/// How fog of war lets this frame's main canvas take the surface routes.
enum class SurfaceFogRoute {
    /// No fog pass touches the main canvas: the routes run unfogged.
    NONE,
    /// The routes run, and the composite fogs each surface-lit sample once.
    COMPOSE,
    /// Fog keeps owning the main canvas's lit colour in compute.
    FALLBACK,
};

/// @p mainCanvasObservers is the main canvas's fog observer block, null when
/// it carries no fog. A line-of-sight gated source keeps its compute march,
/// so any gated source falls back; so does an active glitch subject, whose
/// presentation the composite does not fog.
inline SurfaceFogRoute surfaceFogRoute(
    bool fogPassRegistered,
    const IRComponents::FrameDataFogObservers *mainCanvasObservers,
    bool surfaceLightingEnabled,
    bool glitchPresentationActive
) {
    if (!fogPassRegistered) {
        return SurfaceFogRoute::NONE;
    }
    if (!surfaceLightingEnabled) {
        return SurfaceFogRoute::FALLBACK;
    }
    if (mainCanvasObservers == nullptr) {
        return SurfaceFogRoute::NONE;
    }
    if (mainCanvasObservers->losSourceMask_ != 0 || glitchPresentationActive) {
        return SurfaceFogRoute::FALLBACK;
    }
    return SurfaceFogRoute::COMPOSE;
}

/// Whether @p lighting admits the surface routes.
inline bool enabledFor(const IRComponents::C_PerAxisSurfaceLighting &lighting) {
    return lighting.enabled_.value_or(true);
}

// The lazy `singleton<T>()` create is a `createEntity`, so its first call runs
// on the main thread from `System<LIGHTING_TO_TRIXEL>::create()`.
inline IRComponents::C_PerAxisSurfaceLighting &state() {
    return IREntity::singleton<IRComponents::C_PerAxisSurfaceLighting>();
}

inline void setEnabled(bool enabled) {
    state().enabled_ = enabled;
}

inline bool isEnabled() {
    return enabledFor(state());
}

} // namespace IRPrefab::PerAxisSurfaceLighting

#endif /* PER_AXIS_SURFACE_LIGHTING_H */

#ifndef PER_AXIS_SURFACE_LIGHTING_H
#define PER_AXIS_SURFACE_LIGHTING_H

#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_per_axis_surface_lighting.hpp>

// Per-axis surface lighting: when enabled, an eligible lit pipeline
// (camera-iso per-axis canvas, no debug overlay) shades per-axis voxel faces
// per output fragment at the continuous point on the face, so sun-shadow
// edges on voxel receivers are smooth instead of stepping a whole face at a
// time under camera yaw. Experimental; the per-fragment cost is unmeasured.
// Read every frame, so it can change at runtime.
//
// The same switch admits both surface routes, cardinal and per-axis, on a
// pipeline that registers FOG_TO_TRIXEL; without it fog keeps lighting in
// compute there. See `surfaceFogRoute`.
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

// The lazy `singleton<T>()` create is a `createEntity`, so its first call runs
// on the main thread from `System<LIGHTING_TO_TRIXEL>::create()`.
inline IRComponents::C_PerAxisSurfaceLighting &state() {
    return IREntity::singleton<IRComponents::C_PerAxisSurfaceLighting>();
}

inline void setEnabled(bool enabled) {
    state().enabled_ = enabled;
}

inline bool isEnabled() {
    return state().enabled_;
}

} // namespace IRPrefab::PerAxisSurfaceLighting

#endif /* PER_AXIS_SURFACE_LIGHTING_H */

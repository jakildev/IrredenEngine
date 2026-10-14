#ifndef PER_AXIS_SURFACE_LIGHTING_H
#define PER_AXIS_SURFACE_LIGHTING_H

#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_per_axis_surface_lighting.hpp>

// Per-axis surface lighting: when enabled, an eligible lit pipeline
// (camera-iso per-axis canvas, no fog, no debug overlay) shades per-axis voxel
// faces per output fragment at the continuous point on the face, so sun-shadow
// edges on voxel receivers are smooth instead of stepping a whole face at a
// time under camera yaw. Experimental; the per-fragment cost is unmeasured.
// Read every frame, so it can change at runtime.
namespace IRPrefab::PerAxisSurfaceLighting {

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

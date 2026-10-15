#ifndef COMPONENT_PER_AXIS_SURFACE_LIGHTING_H
#define COMPONENT_PER_AXIS_SURFACE_LIGHTING_H

namespace IRComponents {

// Singleton row holding the per-axis surface lighting switch, read via
// `IRPrefab::PerAxisSurfaceLighting::` (`per_axis_surface_lighting.hpp`).
// `System<LIGHTING_TO_TRIXEL>` seeds it from IR_PERAXIS_SURFACE_LIGHTING=1 at
// registration and reads it once per frame in `beginTick`.
struct C_PerAxisSurfaceLighting {
    bool enabled_ = false;
};

} // namespace IRComponents

#endif /* COMPONENT_PER_AXIS_SURFACE_LIGHTING_H */

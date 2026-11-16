#ifndef COMPONENT_PER_AXIS_SURFACE_LIGHTING_H
#define COMPONENT_PER_AXIS_SURFACE_LIGHTING_H

namespace IRComponents {

// Singleton row holding the per-axis surface lighting switch, read via
// `IRPrefab::PerAxisSurfaceLighting::` (`per_axis_surface_lighting.hpp`).
// `System<LIGHTING_TO_TRIXEL>` applies IR_PERAXIS_SURFACE_LIGHTING (`0` off,
// `1` on) at registration and reads the switch once per frame in `beginTick`.
struct C_PerAxisSurfaceLighting {
    bool enabled_ = true;
};

} // namespace IRComponents

#endif /* COMPONENT_PER_AXIS_SURFACE_LIGHTING_H */

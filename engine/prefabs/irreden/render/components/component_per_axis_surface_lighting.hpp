#ifndef COMPONENT_PER_AXIS_SURFACE_LIGHTING_H
#define COMPONENT_PER_AXIS_SURFACE_LIGHTING_H

#include <optional>

namespace IRComponents {

// Singleton row holding the per-axis surface lighting switch, read via
// `IRPrefab::PerAxisSurfaceLighting::` (`per_axis_surface_lighting.hpp`).
// `System<LIGHTING_TO_TRIXEL>` applies IR_PERAXIS_SURFACE_LIGHTING (`0` off,
// `1` on) at registration and reads the switch once per frame in `beginTick`.
// Unset is on (`enabledFor`).
struct C_PerAxisSurfaceLighting {
    std::optional<bool> enabled_;
};

} // namespace IRComponents

#endif /* COMPONENT_PER_AXIS_SURFACE_LIGHTING_H */

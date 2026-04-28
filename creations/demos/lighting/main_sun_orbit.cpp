// Lighting demo — animated sun direction.
//
// The global sun rotates around the +Z=down vertical so you can see
// shadows sweep across the scene in real time. Validates that:
//
//   1. `IRRender::setSunDirection` updates flow through to every
//      compute pass each frame (occupancy march, analytic SDF march,
//      lambert shading, etc.) without per-frame stale state.
//   2. Shadows project consistently in all azimuth directions, not
//      just the default tilt — a regression in a sun-direction
//      reconstruction term shows up as shadows that vanish or wrap
//      incorrectly at a particular azimuth.
//
// Reuses the default scene (voxel-pool row + SDF row + floor) so the
// shadow direction is the only variable.

#include "common/lighting_demo_main.hpp"

#include <chrono>

namespace {

using IRMath::vec3;

// Capture the start time once on first invocation. The render-pipeline
// tick is the only writer so no synchronisation is needed.
auto &startTime() {
    static const auto t0 = std::chrono::steady_clock::now();
    return t0;
}

void animateSun() {
    const auto now = std::chrono::steady_clock::now();
    const float t = std::chrono::duration<float>(now - startTime()).count();

    // One full revolution every 8 s. Keep a fixed ~30° tilt above the
    // horizon (z = -sin(elevation)) so the sun never grazes the
    // horizon and the lambert dot never collapses to 0 across the
    // scene.
    constexpr float kPeriodSeconds = 8.0f;
    constexpr float kElevationRadians = 0.52f;
    const float azimuth = t * (6.28318530718f / kPeriodSeconds);
    const float horizontal = IRMath::cos(kElevationRadians);
    const vec3 sunDir(
        horizontal * IRMath::cos(azimuth),
        horizontal * IRMath::sin(azimuth),
        -IRMath::sin(kElevationRadians)
    );
    IRRender::setSunDirection(sunDir);
}

} // namespace

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_sun_orbit",
        .tickFn_ = animateSun,
    }
)

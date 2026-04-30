// Lighting demo — sun orbiting in the elevation (Z) axis.
//
// Companion to `main_sun_orbit.cpp`. That demo sweeps the sun through
// every azimuth at a fixed ~30° tilt, which is great for verifying the
// XY-face shading and shadow direction but never tests how the sun's Z
// component drives:
//
//   1. Top-face (Z_FACE) lambert brightness — the dot product is
//      `dot(faceOutwardNormal, sunDir)` and Z_FACE's normal is fixed at
//      (0,0,-1). When the sun is overhead (sunDir.z near -1) the top
//      face should be at full brightness; when the sun is on the
//      horizon (sunDir.z near 0) the top face should darken to ambient.
//   2. Vertical projection of cast shadows — at high noon the shadow
//      collapses to the footprint of the caster; at sunrise/sunset it
//      stretches. Regressions in the iso projection (`+z = down` sign,
//      isoDepthShift, etc.) often only manifest at low elevations and
//      get masked by the ~30° fixed tilt of the azimuth demo.
//
// We hold azimuth fixed (so cast shadows always go in roughly the same
// direction on the floor) and animate elevation from just-above-the-
// horizon to overhead and back.
//
// Reuses the default scene (voxel-pool row + SDF row + floor) so the
// only animated variable is `sunDir.z` plus the matching horizontal
// scaling.

#include "common/lighting_demo_main.hpp"

#include <chrono>

namespace {

using IRMath::vec3;

auto &startTime() {
    static const auto t0 = std::chrono::steady_clock::now();
    return t0;
}

void animateSunElevation() {
    const auto now = std::chrono::steady_clock::now();
    const float t = std::chrono::duration<float>(now - startTime()).count();

    // One full elevation cycle every 8 s. Elevation oscillates between
    // ~5° and ~85° via cos so the sun never passes through the horizon
    // exactly (which would zero out lambert across the scene). The
    // azimuth is fixed at 225° so shadows fall toward (+x, +y) — the
    // same default tilt direction the static demos use, just stretched
    // and compressed by the changing elevation.
    constexpr float kPeriodSeconds = 8.0f;
    constexpr float kAzimuthRadians = 3.92699082f;
    constexpr float kMinElevationRadians = 0.0873f;
    constexpr float kMaxElevationRadians = 1.4835f;
    const float phase = t * (6.28318530718f / kPeriodSeconds);
    const float elevationFraction = 0.5f - 0.5f * IRMath::cos(phase);
    const float elevation =
        kMinElevationRadians +
        elevationFraction * (kMaxElevationRadians - kMinElevationRadians);
    const float horizontal = IRMath::cos(elevation);
    const vec3 sunDir(
        horizontal * IRMath::cos(kAzimuthRadians),
        horizontal * IRMath::sin(kAzimuthRadians),
        -IRMath::sin(elevation)
    );
    IRRender::setSunDirection(sunDir);
}

} // namespace

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_sun_elevation_orbit",
        .tickFn_ = animateSunElevation,
    }
)

#include "common/lighting_demo_main.hpp"

namespace {
// Yaw sweep proving the winning-light-ID cone (#2318) stays world-oriented as
// the camera rotates: cardinal (0°), ~30°, 45°, then a zoomed cardinal look.
// The floor pool must keep its shape/position in world space across all four —
// a camera-locked or mis-oriented cone would slide or distort under yaw.
constexpr IRVideo::AutoScreenshotShot kSpotConeShots[] = {
    {4.0f, IRMath::vec2(0.0f), 0.0f, "spot_cone_yaw0"},
    {4.0f, IRMath::vec2(0.0f), 0.5236f, "spot_cone_yaw30"},
    {4.0f, IRMath::vec2(0.0f), 0.7854f, "spot_cone_yaw45"},
    {7.0f, IRMath::vec2(0.0f), 0.0f, "spot_cone_zoom7"},
};
} // namespace

// Dim the directional sun so the SPOT's winning-light-ID cone (#2318) is the
// dominant light in frame — the cone pool on the floor reads clearly instead
// of washing out under full sun. Ambient stays default so shapes outside the
// cone are still dimly legible.
IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_spot",
        .addSpot_ = true,
        .sunIntensity_ = 0.35f,
        .shots_ = kSpotConeShots,
        .numShots_ = 4,
    }
)

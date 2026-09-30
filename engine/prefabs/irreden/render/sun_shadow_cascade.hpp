#ifndef SUN_SHADOW_CASCADE_H
#define SUN_SHADOW_CASCADE_H

#include <array>

namespace IRPrefab::SunShadow {

// The bake, feeder density and source-face allocation share this cascade policy.
// Map dimensions also mirror ir_sun_projection and ir_sun_face_query_layout.
constexpr int kSunShadowMapDim = 1024;
constexpr float kCascadeSplitRatio = 0.4f;
constexpr float kIsoDepthMin = -256.0f;
constexpr float kIsoDepthMax = 256.0f;
constexpr float kCascadeSplitDepth =
    kIsoDepthMin + (kIsoDepthMax - kIsoDepthMin) * kCascadeSplitRatio;

struct SunCascadeDepthRange {
    float min_;
    float max_;
};

// The far cascade includes the near range so receiver selection can fall back
// to it when a point lies outside the fitted near UV grid.
constexpr std::array kSunCascadeDepthRanges = {
    SunCascadeDepthRange{kIsoDepthMin, kCascadeSplitDepth},
    SunCascadeDepthRange{kIsoDepthMin, kIsoDepthMax}
};
constexpr int kSunShadowCascadeCount = static_cast<int>(kSunCascadeDepthRanges.size());

static_assert(kSunShadowCascadeCount == 2, "FrameDataSun stores exactly two cascade grids");
static_assert(
    kIsoDepthMin < kCascadeSplitDepth && kCascadeSplitDepth < kIsoDepthMax,
    "The near cascade must end strictly inside the far cascade depth range"
);

} // namespace IRPrefab::SunShadow

#endif

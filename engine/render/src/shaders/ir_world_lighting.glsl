// Shared world-space lighting primitives — the light-source list layout, the
// light-volume extent constants, analytic SPOT cone shaping, and the ACES
// tonemap. Consumed by every pass that lights recovered WORLD positions
// (c_lighting_to_trixel, c_light_overflow_faces). Metal twin:
// metal/ir_world_lighting.metal — keep byte-identical math.

// The winning-light ID indexes this list to recover the light's cone axis
// (`directionAndRadius.xyz`), aperture (`coneAndSeedAlpha.x`, full degrees),
// and TRUE apex (`trueOriginVoxel.xyz`). Bound transiently at slot 4 by the
// consuming system; only read on the spot path.
struct GPULightSource {
    vec4 originAndType;
    vec4 colorAndIntensity;
    vec4 directionAndRadius;
    vec4 coneAndSeedAlpha;
    vec4 trueOriginVoxel;
};
layout(std430, binding = 4) readonly buffer LightSourceBuffer {
    GPULightSource lights[];
};

// Mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp.
// The volume covers world voxels in [-half, half) with one texel per
// voxel; sample coords are `(worldVoxel + half + 0.5) / size` to land
// at texel centers.
const float kLightVolumeSize = 128.0;
const float kLightVolumeHalfExtent = 64.0;

// SPOT cone shaping (#2318). Mirrors `LightType::SPOT` in
// component_light_source.hpp. The cone factor smoothly falls off across a
// band from the nominal half-aperture to `kConeEdgeSoftness ×` that angle so
// the cone edge is anti-aliased rather than a hard step.
const int kLightTypeSpot = 3;
const float kConeEdgeSoftness = 1.15;

// Analytic SPOT falloff at world position `pos3D` for the light whose 0-based
// index is `lightIdx`. 1.0 inside the cone, smoothstep to 0.0 across the soft
// edge band, 0.0 outside. The apex is the light's TRUE (unclamped) origin so
// an out-of-window spot's cone stays oriented from its real position.
float spotConeFactor(int lightIdx, vec3 pos3D) {
    const GPULightSource L = lights[lightIdx];
    const vec3 axis = normalize(L.directionAndRadius.xyz);
    const vec3 toCell = pos3D - L.trueOriginVoxel.xyz;
    const float toCellLen = length(toCell);
    if (toCellLen < 1e-4) {
        return 1.0;   // at the apex — fully lit, avoid a 0/0 direction.
    }
    const float cosToCell = dot(toCell / toCellLen, axis);
    const float halfAngle = radians(L.coneAndSeedAlpha.x * 0.5);
    const float cosInner = cos(halfAngle);
    const float cosOuter = cos(min(halfAngle * kConeEdgeSoftness, radians(90.0)));
    return smoothstep(cosOuter, cosInner, cosToCell);
}

// ACES Filmic tone mapping (Stephen Hill's fitted curve).
// Maps [0, ∞) → [0, 1) with a gentle shoulder that preserves color
// saturation in bright highlights better than Reinhard.
vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

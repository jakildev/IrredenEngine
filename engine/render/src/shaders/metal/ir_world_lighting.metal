// Shared world-space lighting primitives — the light-source list layout, the
// light-volume extent constants, analytic SPOT cone shaping, and the ACES
// tonemap. Consumed by every pass that lights recovered WORLD positions
// (c_lighting_to_trixel, c_light_overflow_faces). GLSL twin:
// ../ir_world_lighting.glsl — keep byte-identical math. Metal has no global
// buffer bindings, so consumers pass the light list (`[[buffer(4)]]`) into
// spotConeFactor explicitly.

// Mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp.
// The volume covers world voxels in [-half, half) with one texel per
// voxel; sample coords are `(worldVoxel + half + 0.5) / size` to land
// at texel centers.
constant float kLightVolumeSize = 128.0;
constant float kLightVolumeHalfExtent = 64.0;

// SPOT cone shaping (#2318). Mirrors `LightType::SPOT` in
// component_light_source.hpp. The cone factor smoothly falls off across a
// band from the nominal half-aperture to `kConeEdgeSoftness ×` that angle so
// the cone edge is anti-aliased rather than a hard step.
constant int kLightTypeSpot = 3;
constant float kConeEdgeSoftness = 1.15f;
// Metal has no radians() builtin (GLSL does); convert degrees inline.
constant float kDegToRad = 3.14159265358979323846f / 180.0f;

struct GPULightSource {
    float4 originAndType;
    float4 colorAndIntensity;
    float4 directionAndRadius;
    float4 coneAndSeedAlpha;
    float4 trueOriginVoxel;
};

// LIGHTING_TO_TRIXEL's own frame UBO (`[[buffer(27)]]`), shared by the
// composite pass and the overflow-face relight that runs at its tail — both
// carried byte-identical copies of this layout. The GLSL twins declare the
// equivalent as a per-kernel `layout(std140)` interface block, which cannot be
// hoisted the same way, so this fold is Metal-only.
struct FrameDataLightingToTrixel {
    int   lightingEnabled;
    int   lutEnabled;
    int   lightVolumeEnabled;
    float debugLightLevel;
    int   debugOverlayMode;
    int   hdrEnabled;
    float exposure;
    float skyIntensity;
    float4 skyColor;
};

// The light-volume UBO (`[[buffer(23)]]`), written once by the CPU and read by
// every light-volume stage — the seed, the lighting composite, and the
// overflow relight all bind the SAME buffer, so this layout must stay in
// lockstep across them; that is exactly why it is one definition here rather
// than a copy per kernel.
//
// The seed reads all five fields; the lighting/overflow passes read only
// `worldOriginVoxel` (`.xyz` = the volume's camera-anchored world origin,
// #360 Phase 1c; `.w` = the has-SPOT flag, #2318) and ignore the other four.
//
// c_propagate_light_volume.metal keeps its own copy of this layout on purpose:
// it is the one consumer that does not include this fragment, and pulling the
// light-source list + SPOT/ACES helpers into a kernel dispatched 32× a frame
// to share a struct declaration is not worth it. Keep the two in lockstep.
struct LightVolumeParams {
    int   gridSize;
    int   halfExtent;
    int   lightCount;
    float stepFalloff;
    int4  worldOriginVoxel;
};

// Analytic SPOT falloff at world position `pos3D` for the 0-based light
// `lightIdx`. 1.0 inside the cone, smoothstep to 0.0 across the soft edge
// band, 0.0 outside. Apex is the light's TRUE (unclamped) origin. Mirrors
// the GLSL twin.
float spotConeFactor(device const GPULightSource* lights, int lightIdx, float3 pos3D) {
    const GPULightSource L = lights[lightIdx];
    const float3 axis = normalize(L.directionAndRadius.xyz);
    const float3 toCell = pos3D - L.trueOriginVoxel.xyz;
    const float toCellLen = length(toCell);
    if (toCellLen < 1e-4f) {
        return 1.0f;   // at the apex — fully lit, avoid a 0/0 direction.
    }
    const float cosToCell = dot(toCell / toCellLen, axis);
    const float halfAngle = L.coneAndSeedAlpha.x * 0.5f * kDegToRad;
    const float cosInner = cos(halfAngle);
    const float cosOuter = cos(min(halfAngle * kConeEdgeSoftness, 90.0f * kDegToRad));
    return smoothstep(cosOuter, cosInner, cosToCell);
}

// ACES Filmic tone mapping (Stephen Hill's fitted curve).
// Maps [0, ∞) → [0, 1) with a gentle shoulder that preserves color
// saturation in bright highlights better than Reinhard.
float3 ACESFilm(float3 x) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

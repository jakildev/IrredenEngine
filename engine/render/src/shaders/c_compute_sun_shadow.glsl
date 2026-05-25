#version 450 core

// Per-pixel directional sun shadow compute. For each rasterized surface
// pixel reconstructs the voxel-space surface position from the encoded
// distance texture, projects it into the sun-aligned depth map baked by
// BAKE_SUN_SHADOW_MAP, and compares against the nearest stored blocker.
// Result is a 0..1 brightness factor written into the R channel of the
// canvas sun-shadow texture, consumed later by LIGHTING_TO_TRIXEL.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Must match `kEmptyDistanceEncoded` in c_compute_voxel_ao.glsl.
const int kEmptyDistanceEncoded = 65535;

// Darkening applied to fully-shadowed pixels. 0.45 leaves enough
// detail visible inside shadows; tweak here rather than in the lighting
// pass so the shadow texture stays the single source of truth.
const float kShadowDarken = 0.45;

// Slot 28 is shared with the occupancy grid (read by AO + light-volume).
// The bake / lookup rebind it before their own dispatch, so the alias is
// safe.
layout(std430, binding = 28) readonly buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int _voxelDispatchPadding;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;
};

layout(std140, binding = 29) uniform FrameDataSun {
    // xyz = unit vector pointing from the world toward the sun; w unused.
    uniform vec4 sunDirection;
    uniform float sunIntensity;
    uniform float sunAmbient;
    uniform int shadowsEnabled;
    uniform int aoEnabled;
    uniform vec4 sunBasisU;
    uniform vec4 sunBasisV;
    uniform vec2 sunBufferOriginUV;
    uniform vec2 sunBufferTexelSize;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 1) writeonly uniform image2D canvasSunShadow;

// Must match c_bake_sun_shadow_map.glsl.
const int kSunShadowMapDim = 1024;
const float kSunDepthScale = 1024.0;
const float kSunDepthOffset = 512.0;

float unpackSunDepth(uint packedDepth) {
    return float(packedDepth) / kSunDepthScale - kSunDepthOffset;
}

// Normal-bias offset pushes the lookup point along the face's outward
// normal before projecting into sun-space, preventing self-shadow acne on
// cube tops and SDF spheres caused by adjacent-face pixels rounding to the
// same sun-texel. Tune via render-debug-loop on shape_debug.
const float kNormalBiasVoxels = 0.5;
// Slope-scale bias covers the worst-case sunZ variation between iso pixels
// that share a sun-space texel — roughly texelSize/slope voxels. Below
// that threshold a flat surface self-shadows. Tune via render-debug-loop on shape_debug.
const float kShadowBiasTexelScale = 2.0;
const float kShadowBiasSlopeMin = 0.05;
const float kShadowBiasQuantNoise = 4.0 / kSunDepthScale;

// Reject shadows from occluders farther than this in sun-Z (packed
// depth units). Prevents adjacent volumes from incorrectly casting
// onto faces they are beside rather than in front of.
const float kMaxShadowDepthRange = 24.0 * kSunDepthScale;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(trixelDistances);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= kEmptyDistanceEncoded) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }
    if (shadowsEnabled == 0) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }

    int rawDepth = encoded >> 2;

    // Same reconstruction as c_compute_voxel_ao.glsl — keep the two in
    // lockstep so AO and shadow sample the same voxel cells. sunDirection
    // stays in world coordinates (camera-independent), so only the surface
    // position needs the R(-rasterYaw) compose. At cardinalIndex==0 the
    // path collapses to master so yaw=0 stays byte-identical.
    vec3 pos3D = trixelCanvasPixelToWorld3D(
        pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
    );

    int face = encoded & 3;
    vec3 normal = faceOutwardNormal(face);

    // Screen-space lookup against the bake output. sunZ is negated
    // (smaller = closer to sun) so the bake's atomicMin stores the
    // nearest blocker per texel. The lookup position is offset along the
    // outward normal first so the projected sun-texel shifts away from the
    // true-surface writer, eliminating self-shadow acne without biasing the
    // baked depth map itself.
    vec3 sunDir = sunDirection.xyz;
    vec3 uHat = sunBasisU.xyz;
    vec3 vHat = sunBasisV.xyz;
    vec3 biasedPos3D = pos3D + normal * kNormalBiasVoxels;
    vec2 sunUV = vec2(dot(biasedPos3D, uHat), dot(biasedPos3D, vHat));
    float sunZ = -dot(biasedPos3D, sunDir);

    // Bias depends only on per-fragment constants (face/normal/sunDir,
    // texelSize) — hoisted outside the PCF loop. T-132's slope-scale +
    // quantisation-noise formula stays unchanged; the PCF kernel just
    // applies it to each of the four taps.
    float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
    float texelSize = max(sunBufferTexelSize.x, sunBufferTexelSize.y);
    float bias = texelSize * kShadowBiasTexelScale / slope + kShadowBiasQuantNoise;

    // 2×2 PCF: bilinearly weighted sample of four neighboring sun-space texels.
    // Both bake and lookup use floor() so the texel grid is consistent.
    vec2 sunPxF = (sunUV - sunBufferOriginUV) / sunBufferTexelSize;
    ivec2 base = ivec2(floor(sunPxF));
    vec2 frac = sunPxF - vec2(base);
    float shadowAccum = 0.0;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            ivec2 px = base + ivec2(dx, dy);
            if (px.x < 0 || px.x >= kSunShadowMapDim ||
                px.y < 0 || px.y >= kSunShadowMapDim) continue;
            uint stored = sunDepthBuf[px.y * kSunShadowMapDim + px.x];
            if (stored == 0xFFFFFFFFu) continue;  // no caster → lit
            float nearestZ = unpackSunDepth(stored);
            float weight = mix(1.0 - frac.x, frac.x, float(dx))
                         * mix(1.0 - frac.y, frac.y, float(dy));
            float depthDiff = sunZ - nearestZ;
            if (depthDiff > bias && depthDiff < kMaxShadowDepthRange)
                shadowAccum += weight;
        }
    }

    // Sun-facing faces receive attenuated shadow: a face pointing
    // directly at the sun can only be shadowed by geometry between it
    // and the sun, which the depth-range clamp above already handles.
    // This catches borderline cases where the depth range is marginal.
    float faceSunDot = dot(normal, sunDir);
    if (faceSunDot > 0.3) {
        float atten = smoothstep(0.3, 0.7, faceSunDot);
        shadowAccum *= (1.0 - atten);
    }

    float factor = mix(1.0, kShadowDarken, shadowAccum);
    imageStore(canvasSunShadow, pixel, vec4(factor, 0.0, 0.0, 0.0));
}

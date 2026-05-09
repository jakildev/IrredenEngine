#version 460 core

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

float unpackSunDepth(uint packed) {
    return float(packed) / kSunDepthScale - kSunDepthOffset;
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

    ivec2 sunPx = ivec2(round((sunUV - sunBufferOriginUV) / sunBufferTexelSize));
    bool shadowed = false;
    if (sunPx.x >= 0 && sunPx.x < kSunShadowMapDim &&
        sunPx.y >= 0 && sunPx.y < kSunShadowMapDim) {
        uint storedPacked = sunDepthBuf[sunPx.y * kSunShadowMapDim + sunPx.x];
        if (storedPacked != 0xFFFFFFFFu) {
            float nearestZ = unpackSunDepth(storedPacked);
            float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
            float texelSize = max(sunBufferTexelSize.x, sunBufferTexelSize.y);
            float bias =
                texelSize * kShadowBiasTexelScale / slope + kShadowBiasQuantNoise;
            shadowed = (sunZ - nearestZ) > bias;
        }
    }
    float factor = shadowed ? kShadowDarken : 1.0;
    imageStore(canvasSunShadow, pixel, vec4(factor, 0.0, 0.0, 0.0));
}

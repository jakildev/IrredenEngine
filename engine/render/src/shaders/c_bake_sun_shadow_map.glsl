#version 460 core

// Reconstructs pos3D for each rasterized iso pixel and atomicMin's its
// packed sun-space depth into the sun shadow depth SSBO. Companion to
// c_compute_sun_shadow.glsl's screen-space lookup branch. Design lives at
// docs/design/screen-space-sun-shadow-map.md.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Must match `kEmptyDistanceEncoded` in c_compute_voxel_ao.glsl.
const int kEmptyDistanceEncoded = 65535;
// Must match `kSunShadowMapDim` in c_clear_sun_shadow_map.glsl and the C++
// kSunShadowMapDim in system_bake_sun_shadow_map.hpp.
const int kSunShadowMapDim = 1024;

// Linear remap of signed sun-space depth to monotonic uint for atomicMin.
// Must stay in lockstep with `unpackSunDepth` in c_compute_sun_shadow.glsl —
// a mismatched offset/scale silently breaks the lookup compare.
const float kSunDepthScale = 1024.0;
const float kSunDepthOffset = 512.0;

uint packSunDepth(float sunZ) {
    float biased = clamp(sunZ + kSunDepthOffset, 0.0, kSunDepthOffset * 2.0);
    return uint(biased * kSunDepthScale);
}

layout(std430, binding = 28) restrict buffer SunShadowDepthMap {
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
    uniform vec4 sunDirection;
    uniform float sunIntensity;
    uniform float sunAmbient;
    uniform int shadowsEnabled;
    uniform int shapeCasterCount;
    uniform int occupancyBoundsCount;
    uniform int aoEnabled;
    uniform int useScreenSpaceShadow;
    uniform int _sunPadding0;
    uniform vec4 sunBasisU;
    uniform vec4 sunBasisV;
    uniform vec2 sunBufferOriginUV;
    uniform vec2 sunBufferTexelSize;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(trixelDistances);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= kEmptyDistanceEncoded) {
        return;
    }
    int rawDepth = encoded >> 2;

    // Mirrors c_compute_sun_shadow.glsl pos3D reconstruction — divergence
    // here desyncs the bake/lookup texel handshake.
    int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    int subdivisions = max(voxelRenderOptions.y, 1);
    vec2 canvasOffset = (voxelRenderOptions.x != 0)
        ? frameCanvasOffset * float(subdivisions)
        : frameCanvasOffset;
    ivec2 isoRel =
        pixel - trixelCanvasOffsetZ1 - ivec2(floor(canvasOffset));

    vec3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (voxelRenderOptions.x != 0) {
        pos3D /= float(subdivisions);
    }
    if (cardinalIndex != 0) {
        pos3D = rotateCardinalZInv(pos3D, cardinalIndex);
    }

    // sunZ is negated so smaller = closer to sun (engine's sunDirection
    // points TOWARD the sun, so a raw dot increases moving sunward; the
    // bake's atomicMin needs the opposite sense to keep the nearest blocker).
    vec3 sunDir = sunDirection.xyz;
    vec3 uHat = sunBasisU.xyz;
    vec3 vHat = sunBasisV.xyz;
    vec2 sunUV = vec2(dot(pos3D, uHat), dot(pos3D, vHat));
    float sunZ = -dot(pos3D, sunDir);

    ivec2 sunPx = ivec2(round((sunUV - sunBufferOriginUV) / sunBufferTexelSize));
    if (sunPx.x < 0 || sunPx.x >= kSunShadowMapDim ||
        sunPx.y < 0 || sunPx.y >= kSunShadowMapDim) {
        return;
    }

    uint packed = packSunDepth(sunZ);
    atomicMin(sunDepthBuf[sunPx.y * kSunShadowMapDim + sunPx.x], packed);
}

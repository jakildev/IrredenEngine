#version 450 core

// Reconstructs pos3D for each rasterized iso pixel and atomicMin's its
// packed sun-space depth into both cascade regions of the sun shadow
// depth SSBO. Companion to c_compute_sun_shadow.glsl's screen-space
// lookup branch.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"

const int kEmptyDistanceEncoded = 65535;
const int kSunShadowMapDim = 1024;
const int kCascadeTexelCount = kSunShadowMapDim * kSunShadowMapDim;

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
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single canvas; nonzero = baking
    // a per-axis voxel canvas into the shared sun map (#1311).
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5); used only on the per-axis path (#1311).
    uniform ivec4 visibleFaceIds;
    uniform vec4 _voxelDepthAxisUnused;   // voxelDepthAxis_ in the full UBO (unused here)
    // World-cast offset (#1576 P4b-3). `.xyz` = the opt-in world-placed detached
    // re-voxelize entity's world cell origin; `.w` = 1.0 when the solid opts into
    // world placement, else 0.0. On the second bake dispatch per opt-in detached
    // canvas, recovers each caster voxel's WORLD pos as (model pos + .xyz) so it
    // projects into the SHARED sun map at its true world position (mirrors the
    // receive recovery in c_lighting_to_trixel.glsl). The main + per-axis bakes
    // keep `.w == 0` → byte-identical (the offset is a no-op).
    uniform vec4 detachedWorldReceive;
};

layout(std140, binding = 29) uniform FrameDataSun {
    uniform vec4 sunDirection;
    uniform float sunIntensity;
    uniform float sunAmbient;
    uniform int shadowsEnabled;
    uniform int aoEnabled;
    uniform vec4 sunBasisU;
    uniform vec4 sunBasisV;
    uniform vec2 sunBufferOriginUV;
    uniform vec2 sunBufferTexelSize;
    uniform vec2 cascadeOriginUV_0;
    uniform vec2 cascadeTexelSize_0;
    uniform vec2 cascadeOriginUV_1;
    uniform vec2 cascadeTexelSize_1;
    uniform float cascadeSplitDepth;
    uniform int cascadeCount;
    uniform float _cascadePad0;
    uniform float _cascadePad1;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;

void bakeCascade(vec2 sunUV, float sunZ, vec2 origin, vec2 texelSz, int cascadeOffset) {
    ivec2 sunPx = ivec2(floor((sunUV - origin) / texelSz));
    if (sunPx.x < 0 || sunPx.x >= kSunShadowMapDim ||
        sunPx.y < 0 || sunPx.y >= kSunShadowMapDim) {
        return;
    }
    uint packedDepth = packSunDepth(sunZ);
    atomicMin(sunDepthBuf[cascadeOffset + sunPx.y * kSunShadowMapDim + sunPx.x], packedDepth);
}

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

    // Smooth camera Z-yaw (#1311): the per-axis voxel canvases bake into the same
    // shared sun depth map as the main canvas (SDF/text) so voxels and shapes
    // shadow each other under rotation. A per-axis canvas stores the world frame
    // face-locally; the single canvas stores the cardinal-snapped iso pixel.
    vec3 pos3D = perAxisRoute != 0
        ? perAxisCellToWorld3D(
              pixel, rawDepth, visibleFaceIds[encoded & 3], size,
              frameCanvasOffset, voxelRenderOptions
          )
        : trixelCanvasPixelToWorld3D(
              pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
          );

    // World-cast for an opt-in world-placed detached re-voxelize solid (#1576
    // P4b-3): its distance texture is in the pool-centered MODEL frame, so lift
    // each caster voxel into world space (model pos + the entity world cell
    // origin) before projecting into the sun map. Off (`.w == 0`) → no-op, so the
    // main + per-axis bakes stay byte-identical.
    if (detachedWorldReceive.w != 0.0) {
        pos3D += detachedWorldReceive.xyz;
    }

    vec3 sunDir = sunDirection.xyz;
    vec3 uHat = sunBasisU.xyz;
    vec3 vHat = sunBasisV.xyz;
    vec2 sunUV = vec2(dot(pos3D, uHat), dot(pos3D, vHat));
    float sunZ = -dot(pos3D, sunDir);

    bakeCascade(sunUV, sunZ, cascadeOriginUV_0, cascadeTexelSize_0, 0);
    bakeCascade(sunUV, sunZ, cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount);
}

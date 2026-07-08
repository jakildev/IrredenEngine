#version 450 core

// Reconstructs pos3D for each rasterized iso pixel and atomicMin's its
// packed sun-space depth into both cascade regions of the sun shadow
// depth SSBO. Companion to c_compute_sun_shadow.glsl's screen-space
// lookup branch.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
// Shared caster/receiver sun-space projection + depth pack (#2083).
#include "ir_sun_projection.glsl"

const int kEmptyDistanceEncoded = 65535;

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
    // Buffer-bounds guard, not a culling decision (#2083): a caster outside
    // THIS cascade's UV range is unreadable here by any receiver the sample
    // side accepts — sunCascadeKernelInterior (ir_sun_projection.glsl) routes
    // receivers near the map edge to the covering cascade, whose wider AABB
    // holds this caster's write. Every caster is projected into BOTH cascades
    // below, so this early-out never drops a caster from the pipeline.
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
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        return;
    }
    // Per-axis encoding (#1458): rawDepth in bits [31:10]; single-canvas: bits [31:2].
    int rawDepth = (perAxisRoute != 0) ? (encoded >> 10) : (encoded >> 2);

    // Smooth camera Z-yaw (#1311): the per-axis voxel canvases bake into the same
    // shared sun depth map as the main canvas (SDF/text) so voxels and shapes
    // shadow each other under rotation. A per-axis canvas stores the world frame
    // face-locally; the single canvas stores the cardinal-snapped iso pixel.
    vec3 pos3D;
    if (perAxisRoute != 0) {
        pos3D = perAxisCellToWorld3D(
            pixel, rawDepth, visibleFaceIds[encoded & 3], size,
            frameCanvasOffset, voxelRenderOptions
        );
    } else if (residualYaw != 0.0) {
        // Smooth-yaw cast (#1719). While rotating, the single canvas's
        // remaining SDF/text content is stored at the FULL visualYaw with
        // view-frame depth (#1345/#1370) — recover with the matching smooth
        // inverse so those casters bake at their true world positions. The
        // CARDINAL-layout resolve textures (per-axis #1435 + world-placed
        // P4b-3) bake with residualYaw zeroed by the C++ driver, so they keep
        // the cardinal recovery below.
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, visualYaw
        );
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
    }

    // Shared caster/receiver projection (#2083) — the receiver lookup
    // (ir_sun_shadow_sample.glsl worldSunShadowFactor) derives its sun UV +
    // depth from this same function, so cast and receive cannot drift.
    vec3 sunProj = sunSpaceProject(
        pos3D, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz
    );

    bakeCascade(sunProj.xy, sunProj.z, cascadeOriginUV_0, cascadeTexelSize_0, 0);
    bakeCascade(sunProj.xy, sunProj.z, cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount);
}

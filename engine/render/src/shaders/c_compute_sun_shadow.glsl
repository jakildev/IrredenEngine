#version 450 core

// Per-pixel directional sun shadow compute with cascaded shadow maps.
// For each rasterized surface pixel, reconstructs the voxel-space position,
// selects the appropriate cascade based on iso depth, projects into the
// cascade's sun-aligned depth map, and compares against the nearest stored
// blocker. Blends between cascades at the split boundary.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
// FrameDataSun UBO (29), sun-depth SSBO (28), the cascade PCF sampler, and the
// world-space worldSunShadowFactor() lookup — shared with c_lighting_to_trixel's
// detached world-receive path (#1576 P4b-2).
#include "ir_sun_shadow_sample.glsl"

const int kEmptyDistanceEncoded = 65535;

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single canvas; nonzero = a
    // per-axis canvas bake (#1311), reconstruct world-pos face-locally.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5); used only on the per-axis path (#1278/#1311).
    uniform ivec4 visibleFaceIds;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 1) writeonly uniform image2D canvasSunShadow;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(trixelDistances);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int encoded = imageLoad(trixelDistances, pixel).x;
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }
    if (shadowsEnabled == 0) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }

    // Per-axis encoding (#1458): rawDepth in bits [31:10]; single-canvas: bits [31:2].
    int rawDepth = (perAxisRoute != 0) ? (encoded >> 10) : (encoded >> 2);
    int face = encoded & 3;
    int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
    // face-locally, so recover world-pos via faceOriginFromInPlane and read the
    // world-frame outward normal directly (no cardinal rotation — the store
    // already wrote world coords). The single canvas keeps its cardinal-snap
    // reconstruction + R_z(-rasterYaw) normal rotation, byte-identical at the
    // cardinal fast path (per-axis canvases are only allocated while rotating).
    bool perAxis = perAxisRoute != 0;
    vec3 pos3D;
    vec3 normal;
    if (perAxis) {
        int faceId = visibleFaceIds[face];
        pos3D = perAxisCellToWorld3D(pixel, rawDepth, faceId, size, frameCanvasOffset, voxelRenderOptions);
        normal = faceOutwardNormal6(faceId);
    } else if (residualYaw != 0.0) {
        // Smooth-yaw receive (#1719). While rotating, voxels leave the single
        // canvas (per-axis scatter) and its remaining SDF/text content is
        // stored at the FULL visualYaw with view-frame depth (#1345/#1370) —
        // recover with the matching smooth inverse. The cardinal recovery
        // returns a residual-rotated world pos here, so receivers sampled the
        // sun map off the true surface: the floor shadow froze against the
        // screen, mis-scaled against the deforming floor, and slid off the
        // caster footprint entirely as |residual| grew. residualYaw == 0
        // keeps the byte-identical cardinal path below.
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, visualYaw
        );
        normal = rotateYawZInv(faceOutwardNormal(face), visualYaw);
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
        // Rotate raster-frame face normal to world frame so normal bias and slope
        // bias are applied in the correct world-space direction at non-zero camera
        // yaw. No-op at yaw=0 (cardinalIndex=0). Matches the AO shader pattern.
        normal = rotateCardinalZInv(faceOutwardNormal(face), cardinalIndex);
    }

    // World iso depth picks the cascade; rawDepth IS the world iso depth for the
    // world canvas this pass runs on. The cascade PCF lookup is shared with the
    // detached world-receive path (ir_sun_shadow_sample.glsl, #1576).
    float factor = worldSunShadowFactor(pos3D, normal, float(rawDepth));
    imageStore(canvasSunShadow, pixel, vec4(factor, 0.0, 0.0, 0.0));
}

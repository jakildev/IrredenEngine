#version 450 core

// Screen-space fog-of-war pass. Runs after LIGHTING_TO_TRIXEL and before
// TRIXEL_TO_TRIXEL. For each rasterized pixel, recovers the source voxel's
// world position from the encoded distance + pixel coords and shades it with
// the shared reveal model (ir_fog_common.glsl). Two routes, one kernel:
//   perAxisRoute == 0 — the main canvas, one thread per pixel.
//   perAxisRoute != 0 — a per-axis canvas, one thread per compacted occupied
//                       cell (the per-axis lighting route's args, slots 25/26).
// Background pixels (no rasterized geometry) keep their cleared color, and a
// fully revealed pixel is never read or rewritten.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
#define IR_FOG_LOS_BINDING 4
#include "ir_fog_common.glsl"

// Mirrors FrameDataVoxelToTrixel; std140 layout must match the producer
// declaration in c_voxel_to_trixel_stage_1.glsl.
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    vec2 frameCanvasOffset;
    ivec2 trixelCanvasOffsetZ1;
    ivec2 voxelRenderOptions;
    ivec2 _voxelDispatchGrid;
    int _voxelCount;
    int perAxisRoute;
    ivec2 canvasSizePixels;
    ivec2 _cullIsoMin;
    ivec2 _cullIsoMax;
    float _visualYaw;
    float rasterYaw;
    float _residualYaw;
    float _isDetachedCanvas;
    vec4 _faceDeform[3];
    ivec4 visibleFaceIds;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;
// Read only for the fog whole-body carrier bit (decodeFogWholeBody).
layout(rg32ui, binding = 3) readonly uniform uimage2D triangleCanvasEntityIds;

layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};
layout(std430, binding = 26) readonly buffer PerAxisCellIndirect {
    uint cellDrawArgs[];
};
const uint kDispatchArgsBaseUint = 8u;
const uint kPerAxisCellComputeTile = 256u;

// The three pos3D-recovery shaders (AO, sun shadow, fog) must stay in
// lockstep with the stage-2 encoding. R(-rasterYaw) recovers world coords
// from the cardinal-rotated raster frame.
vec3 fogPixelToWorld(ivec2 pixel, int encoded, int faceId, ivec2 size) {
    if (perAxisRoute != 0) {
        return perAxisCellToWorld3DSubCell(
            pixel,
            encoded,
            faceId,
            size,
            frameCanvasOffset,
            voxelRenderOptions
        );
    }
    return trixelCanvasPixelToWorld3D(
        pixel,
        decodeDepthSingle(encoded),
        trixelCanvasOffsetZ1,
        frameCanvasOffset,
        voxelRenderOptions,
        rasterYaw
    );
}

// The smooth line-of-sight sample of a single-canvas vertical face: the column
// its emitting voxel's face looks into, the voxel recovered from the raster
// (ir_fog_los.glsl).
FogLosSample fogLosPixelFaceSample(ivec2 pixel, int encoded, int worldFaceId) {
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    const ivec3 voxel = fogLosFaceVoxel(
        trixelCanvasPixelToIsoRel(pixel, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions),
        decodeDepthSingle(encoded),
        rotateFaceIdCardinalZ(worldFaceId, cardinalIndex),
        effectiveTrixelSubdivisionScale(voxelRenderOptions),
        voxelRenderOptions.x != 0,
        cardinalIndex
    );
    return fogLosFaceSample(voxel, worldFaceId);
}

void main() {
    const ivec2 size = imageSize(trixelColors);
    ivec2 pixel;
    if (perAxisRoute != 0) {
        const uint groupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
        const uint idx = groupIndex * kPerAxisCellComputeTile + gl_LocalInvocationIndex;
        if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
            return;
        }
        const uint linearCell = compactedCells[idx];
        pixel = ivec2(int(linearCell) % size.x, int(linearCell) / size.x);
    } else {
        pixel = ivec2(gl_GlobalInvocationID.xy);
        if (pixel.x >= size.x || pixel.y >= size.y) {
            return;
        }
    }

    const int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= (perAxisRoute != 0 ? 0x7FFFFFFF : 65535)) {
        return;
    }

    const int slot = decodeSlot(encoded);
    const int faceId = visibleFaceIds[slot] ^ decodeFlipRoute(encoded, perAxisRoute);
    const vec3 pos3D = fogPixelToWorld(pixel, encoded, faceId, size);
    float aaFloor = 0.0;
    bool fogWholeBody = false;
    if (visionCircleCount > 0) {
        // Local world-units-per-pixel from the +x neighbour at the same depth:
        // floors the disc's AA rim at ~1 canvas px at any zoom.
        const vec3 neighbor = fogPixelToWorld(pixel + ivec2(1, 0), encoded, faceId, size);
        aaFloor = length(neighbor.xy - pos3D.xy);
        fogWholeBody = decodeFogWholeBody(imageLoad(triangleCanvasEntityIds, pixel).xy);
    }

    // Only the main canvas carries the smooth gate; a vertical face recovers
    // its voxel from the raster, and only when a smooth source reads it.
    const bool losSmooth = perAxisRoute == 0;
    FogLosSample losSample = fogLosSurfaceSample(pos3D);
    if (losSmooth && (faceId >> 1) != kZFace && fogLosSmoothSampleNeeded(fogWholeBody)) {
        losSample = fogLosPixelFaceSample(pixel, encoded, faceId);
    }

    const FogReveal reveal = fogRevealSample(pos3D, aaFloor, fogWholeBody, losSmooth, losSample);
    if (reveal.state >= 1.0) {
        return;
    }
    const vec4 sourceColor = imageLoad(trixelColors, pixel);
    imageStore(trixelColors, pixel, fogApplyReveal(reveal, faceId >> 1, sourceColor));
}

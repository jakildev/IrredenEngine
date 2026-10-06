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
    float visualYaw;
    float rasterYaw;
    float residualYaw;
    float _isDetachedCanvas;
    vec4 _faceDeform[3];
    ivec4 visibleFaceIds;
    // Members between here and perAxisStoreFrame are declared only to reach
    // its std140 offset.
    vec4 _voxelDepthAxisPadding;
    vec4 _detachedWorldReceivePadding;
    ivec4 _visibleIsoBoundsPadding;
    ivec4 _resolveFeederPadding;
    ivec4 _overflowScratchLayoutPadding;
    ivec4 _overflowSortStepPadding;
    vec4 _detachedViewToWorldPadding;
    // Frame the per-axis store is keyed in: .xy = store cell of the frame's iso
    // origin, .z = cardinal index of the view the key positions are rotated
    // into. FrameDataVoxelToCanvas::perAxisStoreFrame_ (offset 256).
    ivec4 perAxisStoreFrame;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;
// Read only for the fog BODY carrier (decodeFogBody / decodeFogBodyFactor) and
// the analytic-surface carrier bit.
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
// from the cardinal-rotated raster frame; while the camera turns, the single
// canvas holds only smooth-yaw content (voxels scatter per axis), recovered
// with the matching smooth inverse.
vec3 fogPixelToWorld(
    ivec2 pixel,
    int encoded,
    int faceId,
    ivec2 size,
    int cardinalDepth
) {
    if (perAxisRoute != 0) {
        return perAxisCellToWorld3DSubCell(pixel, encoded, faceId, perAxisStoreFrame);
    }
    if (residualYaw != 0.0) {
        return trixelCanvasPixelToWorld3DSmoothYaw(
            pixel,
            decodeDepthSingle(encoded),
            trixelCanvasOffsetZ1,
            frameCanvasOffset,
            voxelRenderOptions,
            visualYaw
        );
    }
    return trixelCanvasPixelToWorld3D(
        pixel,
        cardinalDepth,
        trixelCanvasOffsetZ1,
        frameCanvasOffset,
        voxelRenderOptions,
        rasterYaw
    );
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

    // A hidden body never reaches this pass (its pool range is inactive), so a
    // BODY pixel here is a shown body's, painted at its carrier factor.
    const uvec2 rawId = imageLoad(triangleCanvasEntityIds, pixel).xy;
    if (decodeFogBody(rawId)) {
        const float bodyState = float(decodeFogBodyFactor(rawId)) / 255.0;
        if (bodyState < 1.0) {
            imageStore(
                trixelColors, pixel, fogApplyBody(bodyState, imageLoad(trixelColors, pixel))
            );
        }
        return;
    }

    const int slot = decodeSlot(encoded);
    const int faceId = visibleFaceIds[slot] ^ decodeFlipRoute(encoded, perAxisRoute);
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    const bool analyticCardinal =
        perAxisRoute == 0 && residualYaw == 0.0 && scale > 1 && decodeAnalyticSurface(rawId);
    const int rasterDepth = decodeDepthSingle(encoded);
    const int cardinalDepth = analyticCardinal
        ? rasterDepth - cardinalRasterLatticeDepthOffset(scale)
        : rasterDepth;
    const vec3 pos3D = fogPixelToWorld(pixel, encoded, faceId, size, cardinalDepth);
    float aaFloor = 0.0;
    vec3 losSample = pos3D;
    if (visionCircleCount > 0) {
        // Local world-units-per-pixel from the +x neighbour at the same depth:
        // floors the disc's AA rim at ~1 canvas px at any zoom.
        const vec3 neighbor = fogPixelToWorld(
            pixel + ivec2(1, 0), encoded, faceId, size, cardinalDepth);
        aaFloor = length(neighbor.xy - pos3D.xy);
        if (losSourceMask != 0) {
            // A per-axis cell and a cardinal voxel pixel sit on the raster's
            // lower-corner lattice. Analytic cardinal pixels are restored to
            // their authored surface above; smooth-yaw content is exact.
            int losRoute = kFogLosRouteAnalytic;
            if (perAxisRoute != 0) {
                losRoute = kFogLosRoutePerAxis;
            } else if (residualYaw == 0.0 && !decodeAnalyticSurface(rawId)) {
                losRoute = kFogLosRouteCardinal;
            }
            losSample = fogLosCanonicalSample(
                pos3D,
                faceId,
                losRoute,
                scale
            );
        }
    }

    const FogReveal reveal = fogRevealSample(pos3D, losSample, aaFloor);
    if (reveal.state >= 1.0) {
        return;
    }
    const vec4 sourceColor = imageLoad(trixelColors, pixel);
    imageStore(trixelColors, pixel, fogApplyReveal(reveal, faceId >> 1, sourceColor));
}

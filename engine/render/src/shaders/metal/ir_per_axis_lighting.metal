// Per-axis trixel-canvas world-position reconstruction for the smooth-camera
// Z-yaw lighting passes (T4 / #1311). Mirrors shaders/ir_per_axis_lighting.glsl.
// Kept OUT of ir_iso_common.metal so adding it does not recompile the SDF /
// voxel / scatter shaders that share ir_iso_common — which would perturb their
// FP scheduling and drift SDF-edge pixels at the cardinal fast path (breaking
// the residualYaw == 0 byte-identity guarantee). The metallib build globs every
// *.metal and compiles it standalone, so this file includes ir_iso_common.metal
// (guarded) to resolve its helpers when compiled on its own.
#ifndef IR_PER_AXIS_LIGHTING_METAL_INCLUDED
#define IR_PER_AXIS_LIGHTING_METAL_INCLUDED

#include "ir_iso_common.metal"

// Reconstruct the world-unit surface position of a per-axis trixel canvas cell.
// See ir_per_axis_lighting.glsl for the full rationale. rawDepth is in world units
// (base-resolution encoding, #1458); no subdivision-scale division.
inline float3 perAxisCellToWorld3D(
    int2 cell, int rawDepth, int faceId,
    int2 canvasSize, float2 frameCanvasOffset, int2 voxelRenderOptions
) {
    const int2 perAxisBase =
        trixelFrameOffset(trixelOriginOffsetZ1(canvasSize), frameCanvasOffset, voxelRenderOptions);
    const int3 anchor = faceLocalAnchor(perAxisBase, canvasSize);
    const int axis = faceId >> 1;
    const int2 inPlane = cell - faceLocalBase(axis, anchor, canvasSize);
    return float3(faceOriginFromInPlane(faceId, inPlane, rawDepth));
}

#endif // IR_PER_AXIS_LIGHTING_METAL_INCLUDED

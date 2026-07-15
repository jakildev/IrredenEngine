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
    // Whole-iso base anchor (#1944) — per-axis canvases are base-resolution, so
    // the anchor is NOT density-scaled (voxelRenderOptions retained in the
    // signature for caller compatibility; no longer consulted for the anchor).
    const int2 perAxisBase = trixelOriginOffsetZ1(canvasSize) + int2(floor(frameCanvasOffset));
    // Un-yawed iso recovery — mirror of the scatter + stage 1/2 store.
    // faceId retained in signature for caller compatibility; no longer used in recovery.
    // The store filed this face at `perAxisBase + pos3DtoPos2DIso(facePos)`.
    const int2 isoPix = cell - perAxisBase;
    return isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth));
}

// Sub-cell variant — mirrors ir_per_axis_lighting.glsl. Lattice recovery plus
// the encoding's 4-bit in-plane frac offset (the same reconstruction the
// scatter draws), so absolute-position lighting consumers (light volume,
// sun-shadow receive, overflow relight) sample the surface where it is
// actually rendered. Fractional-positioned content carries up to half a world
// cell here (see #2251); integer content encodes frac 8/8 → zero offset,
// bit-identical to the lattice form.
inline float3 perAxisCellToWorld3DSubCell(
    int2 cell, int encoded, int faceId,
    int2 canvasSize, float2 frameCanvasOffset, int2 voxelRenderOptions
) {
    const float3 origin = perAxisCellToWorld3D(
        cell, decodeDepthPerAxis(encoded), faceId,
        canvasSize, frameCanvasOffset, voxelRenderOptions
    );
    // The shared ir_iso_common decode helpers own the frac-field layout —
    // the same decode peraxis_scatter.metal uses, so lighting recovers
    // exactly the plane the scatter draws.
    const int uFrac4 = decodeUFrac4PerAxis(encoded);
    const int vFrac4 = decodeVFrac4PerAxis(encoded);
    const int wFrac4 = decodeWFrac4PerAxis(encoded);
    float3 eu;
    float3 ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    return origin
        + eu * (float(uFrac4) / 16.0f - 0.5f)
        + ev * (float(vFrac4) / 16.0f - 0.5f)
        + faceOutOfPlaneUnitAxis(faceId >> 1) * (float(wFrac4) / 16.0f - 0.5f);
}

#endif // IR_PER_AXIS_LIGHTING_METAL_INCLUDED

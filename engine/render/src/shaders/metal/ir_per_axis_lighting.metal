// Per-axis trixel-canvas world-position reconstruction for the smooth-camera
// Z-yaw lighting passes. Mirrors shaders/ir_per_axis_lighting.glsl.
// Kept OUT of ir_iso_common.metal because growing ir_iso_common changes the SDF /
// voxel / scatter shaders that share it, which perturbs their FP scheduling and
// drifts SDF-edge pixels at the cardinal fast path (breaking the
// residualYaw == 0 byte-identity guarantee). Six kernels include this file
// — the five lighting compute passes (c_compute_voxel_ao, c_bake_sun_shadow_map,
// c_compute_sun_shadow, c_lighting_to_trixel, c_light_overflow_faces) plus the
// per-axis sun-shadow cast/resolve bridge (c_resolve_per_axis_screen_depth) —
// and that set IS the blast radius of editing it. This file includes
// ir_iso_common.metal (guarded) to resolve its helpers and stay self-contained
// as an include-fragment; the runtime preprocessor's visited-set (metal_pipeline.cpp)
// dedupes the include when a wrapper also pulls in ir_iso_common.metal directly.
#ifndef IR_PER_AXIS_LIGHTING_METAL_INCLUDED
#define IR_PER_AXIS_LIGHTING_METAL_INCLUDED

#include "ir_iso_common.metal"

// Reconstruct the world-unit surface position of a per-axis trixel canvas cell.
// rawDepth is in world units (base-resolution encoding); no subdivision-scale
// division. `faceId` and `voxelRenderOptions` are unused by the recovery.
inline float3 perAxisCellToWorld3D(
    int2 cell, int rawDepth, int faceId,
    int2 canvasSize, float2 frameCanvasOffset, int2 voxelRenderOptions
) {
    // Whole-iso base anchor — per-axis canvases are base-resolution, so the
    // anchor is NOT density-scaled.
    const int2 perAxisBase = trixelOriginOffsetZ1(canvasSize) + int2(floor(frameCanvasOffset));
    // Un-yawed iso recovery — mirror of the scatter + stage 1/2 store, which
    // filed this face at `perAxisBase + pos3DtoPos2DIso(facePos)`.
    const int2 isoPix = cell - perAxisBase;
    return isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth));
}

// The encoding's three 4-bit sub-cell offsets, decoded and re-centred to
// signed world-unit fractions along the face's own (e_u, e_v, n) basis —
// mirrors ir_per_axis_lighting.glsl. Single definition of the centring
// convention: perAxisCellToWorld3DSubCell composes it in the WORLD frame, the
// per-axis sun-shadow CAST bridge (c_resolve_per_axis_screen_depth.metal)
// composes it against the same basis already rotated into the cardinal VIEW
// frame, and rotation is linear, so both reach the same surface.
inline float3 perAxisSubCellFrac(int encoded) {
    // The shared ir_iso_common decode helpers own the frac-field bit layout —
    // the same decode peraxis_scatter.metal uses.
    return float3(
        float(decodeUFrac4PerAxis(encoded)) / 16.0f - 0.5f,
        float(decodeVFrac4PerAxis(encoded)) / 16.0f - 0.5f,
        float(decodeWFrac4PerAxis(encoded)) / 16.0f - 0.5f
    );
}

// Sub-cell variant — mirrors ir_per_axis_lighting.glsl. Lattice recovery plus
// the encoding's 4-bit frac offset (the same reconstruction the scatter draws),
// so absolute-position lighting consumers (light volume, sun-shadow receive,
// overflow relight, and the sun-shadow CAST bridge) sample the surface where it
// is actually rendered. Fractional-positioned content carries up to half a
// world cell here, and a lattice-only recovery samples INSIDE the solid;
// integer content encodes frac 8/8 → zero offset, bit-identical to the
// lattice form.
inline float3 perAxisCellToWorld3DSubCell(
    int2 cell, int encoded, int faceId,
    int2 canvasSize, float2 frameCanvasOffset, int2 voxelRenderOptions
) {
    const float3 origin = perAxisCellToWorld3D(
        cell, decodeDepthPerAxis(encoded), faceId,
        canvasSize, frameCanvasOffset, voxelRenderOptions
    );
    const float3 frac = perAxisSubCellFrac(encoded);
    float3 eu;
    float3 ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    return origin + eu * frac.x + ev * frac.y +
           faceOutOfPlaneUnitAxis(faceId >> 1) * frac.z;
}

#endif // IR_PER_AXIS_LIGHTING_METAL_INCLUDED

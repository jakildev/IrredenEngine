// Per-axis trixel-canvas world-position reconstruction for the smooth-camera
// Z-yaw lighting passes. Mirrors shaders/ir_per_axis_lighting.glsl.
// Kept OUT of ir_iso_common.metal because growing ir_iso_common changes the SDF /
// voxel / scatter shaders that share it, which perturbs their FP scheduling and
// drifts SDF-edge pixels at the cardinal fast path (breaking the
// residualYaw == 0 byte-identity guarantee). The lighting compute passes
// (c_compute_voxel_ao, c_bake_sun_shadow_map,
// c_compute_sun_shadow, c_lighting_to_trixel, c_light_overflow_faces) plus the
// fog compute passes and per-axis sun-shadow cast/resolve bridge
// (c_resolve_per_axis_screen_depth) include it. That set is the blast radius of
// editing it. This file includes
// ir_iso_common.metal (guarded) to resolve its helpers and stay self-contained
// as an include-fragment; the runtime preprocessor's visited-set (metal_pipeline.cpp)
// dedupes the include when a wrapper also pulls in ir_iso_common.metal directly.
#ifndef IR_PER_AXIS_LIGHTING_METAL_INCLUDED
#define IR_PER_AXIS_LIGHTING_METAL_INCLUDED

#include "ir_iso_common.metal"

// Reconstruct the uncentered world-unit face origin of a per-axis canvas cell.
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

// Decode the face origin O, including signed sixteenth-cell phase. Scatter
// centers its displayed square by subtracting (0.5,0.5,0.5) from each corner;
// a sun receiver uses perAxisFaceCenter rather than treating O as that center.
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

// The scatter draws O + u*eu + v*ev - 0.5. Polarity is already in O,
// so its square center subtracts half the positive axis for either sign.
inline float3 perAxisFaceCenter(float3 faceOrigin, int faceId) {
    return faceOrigin - 0.5 * faceOutOfPlaneUnitAxis(faceId >> 1);
}

#endif // IR_PER_AXIS_LIGHTING_METAL_INCLUDED

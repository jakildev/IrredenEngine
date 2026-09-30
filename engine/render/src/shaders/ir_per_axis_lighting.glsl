// Per-axis trixel-canvas world-position reconstruction for the smooth-camera
// Z-yaw lighting passes. Kept in a dedicated include — NOT in
// ir_iso_common.glsl — because growing ir_iso_common changes the SDF / voxel /
// scatter shaders that share it, which perturbs their floating-point
// instruction scheduling and drifts a few SDF-edge pixels at the cardinal fast
// path (breaking the residualYaw == 0 byte-identity guarantee). The lighting
// compute passes (c_compute_voxel_ao,
// c_bake_sun_shadow_map, c_compute_sun_shadow, c_lighting_to_trixel,
// c_light_overflow_faces), fog compute passes, and the per-axis sun-shadow
// cast/resolve bridge (c_resolve_per_axis_screen_depth) include it. That set is
// the blast radius of
// editing it. All include it AFTER ir_iso_common.glsl (whose helpers —
// trixelFrameOffset, trixelOriginOffsetZ1, isoPixelToPos3D,
// effectiveTrixelSubdivisionScale — this builds on). GLSL's include resolver
// is recursive with a visited-set cycle guard (opengl_shader.cpp
// `resolveShaderIncludes`), so this fragment self-includes ir_iso_common.glsl
// to resolve those helpers and stay self-contained; a wrapper's earlier
// include of ir_iso_common.glsl makes this a suppressed duplicate (mirrors
// ir_per_axis_lighting.metal).
#include "ir_iso_common.glsl"

// Reconstruct the uncentered world-unit face origin of a per-axis canvas cell.
// The per-axis store (base-resolution encoding) keys each cell by its
// un-yawed (cardinal) iso pixel `perAxisBase + pos3DtoPos2DIso(facePos)`; this
// recovers the face origin by the exact iso inverse the forward scatter uses
// (isoPixelToPos3D — no 2cos(yaw)+1 singularity, since the index is un-yawed).
// rawDepth is already in world units — no scale division required.
// `faceId` and `voxelRenderOptions` are unused by the recovery.
// `canvasSize` is the per-axis canvas size (= imageSize).
vec3 perAxisCellToWorld3D(
    ivec2 cell, int rawDepth, int faceId,
    ivec2 canvasSize, vec2 frameCanvasOffset, ivec2 voxelRenderOptions
) {
    // Whole-iso base anchor — per-axis canvases are base-resolution, so the
    // anchor is NOT density-scaled.
    ivec2 perAxisBase = trixelOriginOffsetZ1(canvasSize) + ivec2(floor(frameCanvasOffset));
    ivec2 isoPix = cell - perAxisBase;
    return isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth));
}

// The encoding's three 4-bit sub-cell offsets, decoded and re-centred to
// signed world-unit fractions along the face's own (e_u, e_v, n) basis —
// x/y in-plane (faceInPlaneUnitAxes), z out-of-plane
// (faceOutOfPlaneUnitAxis). Each component lands in [-0.5, +7/16]; integer-
// positioned content encodes 8/8/8 and yields exactly (0,0,0).
//
// This is the single definition of the frac layout's CENTRING convention, and
// it is deliberately basis-free: perAxisCellToWorld3DSubCell composes it in
// the WORLD frame, while the per-axis sun-shadow CAST bridge
// (c_resolve_per_axis_screen_depth) composes it against the same basis already
// rotated into the cardinal VIEW frame. Rotation is linear, so both reach the
// same surface.
vec3 perAxisSubCellFrac(int encoded) {
    // The shared ir_iso_common decode helpers own the frac-field bit layout —
    // the same decode v_peraxis_scatter uses.
    return vec3(
        float(decodeUFrac4PerAxis(encoded)) / 16.0 - 0.5,
        float(decodeVFrac4PerAxis(encoded)) / 16.0 - 0.5,
        float(decodeWFrac4PerAxis(encoded)) / 16.0 - 0.5
    );
}

// Decode the face origin O, including signed sixteenth-cell phase. Scatter
// centers its displayed square by subtracting (0.5,0.5,0.5) from each corner;
// a sun receiver uses perAxisFaceCenter rather than treating O as that center.
vec3 perAxisCellToWorld3DSubCell(
    ivec2 cell, int encoded, int faceId,
    ivec2 canvasSize, vec2 frameCanvasOffset, ivec2 voxelRenderOptions
) {
    const vec3 origin = perAxisCellToWorld3D(
        cell, decodeDepthPerAxis(encoded), faceId,
        canvasSize, frameCanvasOffset, voxelRenderOptions
    );
    const vec3 frac = perAxisSubCellFrac(encoded);
    vec3 eu, ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    return origin + eu * frac.x + ev * frac.y +
           faceOutOfPlaneUnitAxis(faceId >> 1) * frac.z;
}

// The scatter draws O + u*eu + v*ev - 0.5. Polarity is already in O,
// so its square center subtracts half the positive axis for either sign.
vec3 perAxisFaceCenter(vec3 faceOrigin, int faceId) {
    return faceOrigin - 0.5 * faceOutOfPlaneUnitAxis(faceId >> 1);
}

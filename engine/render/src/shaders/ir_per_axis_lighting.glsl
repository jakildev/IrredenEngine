// Per-axis trixel-canvas world-position reconstruction for the smooth-camera
// Z-yaw lighting passes (T4 / #1311). Kept in a dedicated include — NOT in
// ir_iso_common.glsl — so adding it does not recompile the SDF / voxel / scatter
// shaders that share ir_iso_common, which would perturb their floating-point
// instruction scheduling and drift a few SDF-edge pixels at the cardinal fast
// path (breaking the residualYaw == 0 byte-identity guarantee). Six shaders
// include this file — the five lighting compute passes (c_compute_voxel_ao,
// c_bake_sun_shadow_map, c_compute_sun_shadow, c_lighting_to_trixel,
// c_light_overflow_faces) plus the per-axis sun-shadow cast/resolve bridge
// (c_resolve_per_axis_screen_depth) — and that set IS the blast radius of
// editing it. All include it AFTER ir_iso_common.glsl (whose helpers —
// trixelFrameOffset, trixelOriginOffsetZ1, isoPixelToPos3D,
// effectiveTrixelSubdivisionScale — this builds on). GLSL's include resolver
// is recursive with a visited-set cycle guard (opengl_shader.cpp
// `resolveShaderIncludes`), so this fragment self-includes ir_iso_common.glsl
// below to resolve those helpers and stay self-contained; a wrapper's earlier
// include of ir_iso_common.glsl makes this a suppressed duplicate (mirrors
// ir_per_axis_lighting.metal).
#include "ir_iso_common.glsl"

// Reconstruct the world-unit surface position of a per-axis trixel canvas cell.
// The per-axis store (#1458: base-resolution encoding) keys each cell by its
// un-yawed (cardinal) iso pixel `perAxisBase + pos3DtoPos2DIso(facePos)`; this
// recovers the face origin by the exact iso inverse the forward scatter uses
// (isoPixelToPos3D — no 2cos(yaw)+1 singularity, since the index is un-yawed).
// rawDepth is already in world units — no scale division required.
// faceId retained in signature for caller compatibility; no longer used in recovery.
// `canvasSize` is the per-axis canvas size (= imageSize).
vec3 perAxisCellToWorld3D(
    ivec2 cell, int rawDepth, int faceId,
    ivec2 canvasSize, vec2 frameCanvasOffset, ivec2 voxelRenderOptions
) {
    // Whole-iso base anchor (#1944) — per-axis canvases are base-resolution, so
    // the anchor is NOT density-scaled (voxelRenderOptions retained in the
    // signature for caller compatibility; no longer consulted for the anchor).
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
// it is deliberately basis-free: the sub-cell recovery below composes it in
// the WORLD frame, while the per-axis sun-shadow CAST bridge
// (c_resolve_per_axis_screen_depth) composes it against the same basis already
// rotated into the cardinal VIEW frame. Rotation is linear, so both reach the
// same surface — and both read the layout from here, so the two seams cannot
// drift the way an inlined copy of the recovery did (#2816).
vec3 perAxisSubCellFrac(int encoded) {
    // The shared ir_iso_common decode helpers own the frac-field bit layout —
    // the same decode v_peraxis_scatter uses.
    return vec3(
        float(decodeUFrac4PerAxis(encoded)) / 16.0 - 0.5,
        float(decodeVFrac4PerAxis(encoded)) / 16.0 - 0.5,
        float(decodeWFrac4PerAxis(encoded)) / 16.0 - 0.5
    );
}

// Sub-cell variant: the lattice recovery above plus the encoding's 4-bit
// frac offset — the SAME reconstruction v_peraxis_scatter draws, so
// lighting samples the surface where it is actually rendered. The frac is not
// a sub-pixel nicety: fractional-positioned content (a voxel mid-glide, the
// roundHalfUp tie convention placing half-integer content at cell − 0.5)
// carries up to half a world cell here, and a lattice-only recovery samples
// the light volume / sun map INSIDE the solid on every camera-facing surface
// (see #2251). Integer-positioned content encodes frac 8/8 → zero offset, so
// it is bit-identical to the lattice recovery. Consumers whose output
// provably cancels the in-plane offset (AO's outward-normal height dot) may
// keep the cheaper lattice form; absolute-position consumers (light volume,
// sun-shadow receive, overflow relight, and the sun-shadow CAST bridge —
// #2816) must recover with the frac applied.
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

// Per-axis trixel-canvas world-position reconstruction for the smooth-camera
// Z-yaw lighting passes (T4 / #1311). Kept in a dedicated include — NOT in
// ir_iso_common.glsl — so adding it does not recompile the SDF / voxel / scatter
// shaders that share ir_iso_common, which would perturb their floating-point
// instruction scheduling and drift a few SDF-edge pixels at the cardinal fast
// path (breaking the residualYaw == 0 byte-identity guarantee). Only the four
// lighting compute shaders include this file, AFTER ir_iso_common.glsl (whose
// helpers — trixelFrameOffset, trixelOriginOffsetZ1, isoPixelToPos3D,
// effectiveTrixelSubdivisionScale — this builds on).

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

// Sub-cell variant: the lattice recovery above plus the encoding's 4-bit
// in-plane frac offset — the SAME reconstruction v_peraxis_scatter draws, so
// lighting samples the surface where it is actually rendered. The frac is not
// a sub-pixel nicety: fractional-positioned content (a voxel mid-glide, the
// roundHalfUp tie convention placing half-integer content at cell − 0.5)
// carries up to half a world cell here, and a lattice-only recovery samples
// the light volume / sun map INSIDE the solid on every camera-facing surface
// (see #2251). Integer-positioned content encodes frac 8/8 → zero offset, so
// it is bit-identical to the lattice recovery. Consumers whose output
// provably cancels the in-plane offset (AO's outward-normal height dot) may
// keep the cheaper lattice form; absolute-position consumers (light volume,
// sun-shadow receive, overflow relight) must use this one.
vec3 perAxisCellToWorld3DSubCell(
    ivec2 cell, int encoded, int faceId,
    ivec2 canvasSize, vec2 frameCanvasOffset, ivec2 voxelRenderOptions
) {
    const vec3 origin = perAxisCellToWorld3D(
        cell, decodeDepthPerAxis(encoded), faceId,
        canvasSize, frameCanvasOffset, voxelRenderOptions
    );
    // The shared ir_iso_common decode helpers own the frac-field layout —
    // the same decode v_peraxis_scatter uses, so lighting recovers exactly
    // the plane the scatter draws.
    const int uFrac4 = decodeUFrac4PerAxis(encoded);
    const int vFrac4 = decodeVFrac4PerAxis(encoded);
    const int wFrac4 = decodeWFrac4PerAxis(encoded);
    vec3 eu, ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    return origin
        + eu * (float(uFrac4) / 16.0 - 0.5)
        + ev * (float(vFrac4) / 16.0 - 0.5)
        + faceOutOfPlaneUnitAxis(faceId >> 1) * (float(wFrac4) / 16.0 - 0.5);
}

// Per-axis trixel-canvas world-position reconstruction for the smooth-camera
// Z-yaw lighting passes (T4 / #1311). Kept in a dedicated include — NOT in
// ir_iso_common.glsl — so adding it does not recompile the SDF / voxel / scatter
// shaders that share ir_iso_common, which would perturb their floating-point
// instruction scheduling and drift a few SDF-edge pixels at the cardinal fast
// path (breaking the residualYaw == 0 byte-identity guarantee). Only the four
// lighting compute shaders include this file, AFTER ir_iso_common.glsl (whose
// helpers — trixelFrameOffset, trixelOriginOffsetZ1, faceLocalAnchor,
// faceLocalBase, faceOriginFromInPlane, effectiveTrixelSubdivisionScale — this
// builds on).

// Reconstruct the world-unit surface position of a per-axis trixel canvas cell.
// The per-axis store (c_voxel_to_trixel_stage_1, perAxisRoute != 0) keys each
// cell face-locally; this recovers the face origin by the exact integer inverse
// the forward scatter uses (faceOriginFromInPlane — no 2cos(yaw)+1 singularity),
// then divides by the subdivision scale to land in WORLD voxel units so the
// recovered position matches the cardinal path's trixelCanvasPixelToWorld3D and
// can sample the SHARED world-space light volume + project into the shared sun
// depth map. `faceId` is the world FaceId (visibleFaceIds[slot]); NO cardinal
// rotation — the per-axis store writes the world frame directly
// (pos3DtoPos2DIsoYawed, not the cardinal snap). `canvasSize` is the per-axis
// canvas size (= imageSize), from which perAxisBase is reproduced identically to
// the store (its trixelCanvasOffsetZ1 is trixelOriginOffsetZ1(canvasSize)).
vec3 perAxisCellToWorld3D(
    ivec2 cell, int rawDepth, int faceId,
    ivec2 canvasSize, vec2 frameCanvasOffset, ivec2 voxelRenderOptions
) {
    ivec2 perAxisBase =
        trixelFrameOffset(trixelOriginOffsetZ1(canvasSize), frameCanvasOffset, voxelRenderOptions);
    ivec3 anchor = faceLocalAnchor(perAxisBase, canvasSize);
    int axis = faceId >> 1;
    ivec2 inPlane = cell - faceLocalBase(axis, anchor, canvasSize);
    vec3 pos3D = vec3(faceOriginFromInPlane(faceId, inPlane, rawDepth));
    int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    if (scale > 1) {
        pos3D /= float(scale);
    }
    return pos3D;
}

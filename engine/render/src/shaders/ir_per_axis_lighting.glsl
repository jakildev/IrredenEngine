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
// The per-axis store (#1458: base-resolution encoding) keys each cell face-locally
// in world units; this recovers the face origin by the exact integer inverse the
// forward scatter uses (faceOriginFromInPlane — no 2cos(yaw)+1 singularity).
// rawDepth is already in world units — no scale division required.
// `faceId` is the world FaceId (visibleFaceIds[slot]); NO cardinal rotation —
// the per-axis store writes the world frame directly (pos3DtoPos2DIsoYawed).
// `canvasSize` is the per-axis canvas size (= imageSize).
vec3 perAxisCellToWorld3D(
    ivec2 cell, int rawDepth, int faceId,
    ivec2 canvasSize, vec2 frameCanvasOffset, ivec2 voxelRenderOptions
) {
    ivec2 perAxisBase =
        trixelFrameOffset(trixelOriginOffsetZ1(canvasSize), frameCanvasOffset, voxelRenderOptions);
    ivec3 anchor = faceLocalAnchor(perAxisBase, canvasSize);
    int axis = faceId >> 1;
    ivec2 inPlane = cell - faceLocalBase(axis, anchor, canvasSize);
    return vec3(faceOriginFromInPlane(faceId, inPlane, rawDepth));
}

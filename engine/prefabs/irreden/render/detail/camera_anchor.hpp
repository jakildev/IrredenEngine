#ifndef IRREDEN_RENDER_DETAIL_CAMERA_ANCHOR_H
#define IRREDEN_RENDER_DETAIL_CAMERA_ANCHOR_H

// Phase 1c (#360): camera-anchored occupancy + light-volume grids.
//
// `BUILD_OCCUPANCY_GRID` and `COMPUTE_LIGHT_VOLUME` both need a single
// world-voxel position to center their grid on each frame. Both must
// agree (so the propagate shader's occupancy lookups land in the same
// cells the bitfield was written into) — this header is the single
// source of truth for that derivation.
//
// The iso camera position lives in 2D iso space (`vec2`); we invert
// the iso projection at `z = 0` to recover the world voxel the camera
// is looking at, then snap to integer voxel coords (the 1-voxel snap
// avoids fractional shifts entirely; future incremental-update work
// can raise the snap quantum to chunk size for cheaper recenters).

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

namespace IRRender::detail {

/// Derive the world voxel the iso camera is currently centered on.
/// Inverse of `IRMath::pos3DtoPos2DIso` evaluated at `z = 0`:
///   `iso.x = -x + y, iso.y = -x - y` ⇒
///   `x = -(iso.x + iso.y) / 2, y = (iso.x - iso.y) / 2`
/// Result is rounded to the nearest integer voxel.
inline IRMath::ivec3 cameraAnchorVoxel() {
    const IRMath::vec2 iso = IRRender::getCameraPosition2DIso();
    const float worldX = -(iso.x + iso.y) * 0.5f;
    const float worldY = (iso.x - iso.y) * 0.5f;
    return IRMath::ivec3(IRMath::round(worldX), IRMath::round(worldY), 0);
}

} // namespace IRRender::detail

#endif // IRREDEN_RENDER_DETAIL_CAMERA_ANCHOR_H

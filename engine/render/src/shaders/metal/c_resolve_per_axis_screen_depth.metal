#include "ir_iso_common.metal"
#include <metal_atomic>

// Mirrors shaders/c_resolve_per_axis_screen_depth.glsl. Re-projects one
// face-local per-axis voxel canvas into a screen-space front-most iso-depth
// scratch buffer laid out exactly like the main canvas distance texture, so
// BAKE_SUN_SHADOW_MAP can cast per-axis voxel shadows through its existing
// cardinal recovery (#1435). Scratch is a buffer (not a texture) because MSL
// has no portable image-atomic syntax — same pattern as
// c_voxel_to_trixel_stage_1.metal's distance scratch.

constant int kEmptyDistanceEncoded = 65535;

kernel void c_resolve_per_axis_screen_depth(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    texture2d<int, access::read> perAxisDistances [[texture(0)]],
    device atomic_int* resolveScratch [[buffer(28)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 cell = int2(globalId.xy);
    const int2 perAxisSize =
        int2(int(perAxisDistances.get_width()), int(perAxisDistances.get_height()));
    if (cell.x >= perAxisSize.x || cell.y >= perAxisSize.y) {
        return;
    }

    const int rawDist = perAxisDistances.read(uint2(cell)).x;
    if (rawDist >= kEmptyDistanceEncoded) {
        return; // empty per-axis cell
    }
    const int rawDepth = rawDist >> 2;
    const int slot = rawDist & 3;
    const int faceId = frameData.visibleFaceIds[slot];
    const int axis = faceId >> 1;

    // Recover the face-plane origin (canvas-native units) — exact integer
    // inverse, identical to perAxisCellToWorld3D / peraxis_scatter.metal.
    const int2 perAxisBase = trixelFrameOffset(
        trixelOriginOffsetZ1(perAxisSize),
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );
    const int3 anchor = faceLocalAnchor(perAxisBase, perAxisSize);
    const int2 inPlane = cell - faceLocalBase(axis, anchor, perAxisSize);
    const int3 origin = faceOriginFromInPlane(faceId, inPlane, rawDepth);

    // Re-project into the MAIN-canvas cardinal distance layout, mirroring
    // c_voxel_to_trixel_stage_1.metal's cardinal store, so the BAKE cardinal
    // recovery (trixelCanvasPixelToWorld3D) inverts it exactly and agrees with
    // the per-axis RECEIVE (perAxisCellToWorld3D) by construction.
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    const int scale = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    int3 viewPos = origin;
    if (cardinalIndex != 0) {
        viewPos = rotateCardinalZ(origin, cardinalIndex);
        viewPos += cardinalLowerCornerShift(cardinalIndex) * scale;
    }
    const int encoded = encodeDepthWithFace(pos3DtoDistance(viewPos), slot);

    const int2 canvasSize = frameData.canvasSizePixels; // MAIN canvas size
    const int2 mainBase = trixelFrameOffset(
        trixelOriginOffsetZ1(canvasSize),
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );
    const int2 mainPixel = mainBase + pos3DtoPos2DIso(viewPos);
    if (mainPixel.x < 0 || mainPixel.x >= canvasSize.x ||
        mainPixel.y < 0 || mainPixel.y >= canvasSize.y) {
        return;
    }

    const uint idx = uint(mainPixel.y) * uint(canvasSize.x) + uint(mainPixel.x);
    atomic_fetch_min_explicit(&resolveScratch[idx], encoded, memory_order_relaxed);
}

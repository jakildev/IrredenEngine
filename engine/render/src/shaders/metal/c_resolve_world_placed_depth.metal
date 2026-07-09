#include "ir_iso_common.metal"
#include <metal_atomic>

// Mirrors shaders/c_resolve_world_placed_depth.glsl. Re-projects one opt-in
// world-placed detached re-voxelize canvas (model-frame R32I distance texture)
// into a screen-space front-most iso-depth scratch buffer laid out exactly
// like the main canvas distance texture, so BAKE_SUN_SHADOW_MAP can cast it
// through its existing cardinal recovery (#1576 P4b-3, Q2 mechanism a′).
// Scratch is a buffer (not a texture) because MSL has no portable image-atomic
// syntax — same pattern as c_resolve_per_axis_screen_depth.metal.

constant int kEmptyDistanceEncoded = 65535;

kernel void c_resolve_world_placed_depth(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    texture2d<int, access::read> detachedDistances [[texture(0)]],
    device atomic_int* resolveScratch [[buffer(28)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 cell = int2(globalId.xy);
    const int2 detachedSize =
        int2(int(detachedDistances.get_width()), int(detachedDistances.get_height()));
    if (cell.x >= detachedSize.x || cell.y >= detachedSize.y) {
        return;
    }

    const int rawDist = detachedDistances.read(uint2(cell)).x;
    if (rawDist >= kEmptyDistanceEncoded) {
        return; // empty detached cell
    }
    // Single-canvas encoding (flip carrier #2207): the flip is re-emitted into
    // the re-projected encode below so polarity survives the resolve bridge.
    const int rawDepth = decodeDepthSingle(rawDist);
    const int slot = decodeSlot(rawDist);
    const int flip = decodeFlipSingle(rawDist);

    // Recover the pool-centered MODEL position in subdivision units — the
    // integer-unit form of the trixelCanvasPixelToWorld3D recovery the
    // world-receive lighting path uses (cast and receive agree).
    const int2 detachedBase = trixelFrameOffset(
        trixelOriginOffsetZ1(detachedSize),
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );
    const int2 isoRel = cell - detachedBase;
    const float3 modelPos = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));

    // Lift to WORLD: the world cell origin is in world cells; the model
    // recovery is in subdivision units, so the offset scales by the factor.
    const int scale = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    const int3 worldPos =
        roundHalfUp(modelPos + frameData.detachedWorldReceive.xyz * float(scale));

    // Re-project into the MAIN-canvas cardinal distance layout, mirroring
    // c_voxel_to_trixel_stage_1.metal's cardinal store, so the BAKE cardinal
    // recovery (trixelCanvasPixelToWorld3D) inverts it exactly.
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    int3 viewPos = worldPos;
    if (cardinalIndex != 0) {
        viewPos = rotateCardinalZ(worldPos, cardinalIndex);
        viewPos += cardinalLowerCornerShift(cardinalIndex) * scale;
    }
    const int encoded = encodeDepthWithFace(pos3DtoDistance(viewPos), slot, flip);

    const int2 canvasSize = frameData.canvasSizePixels; // MAIN canvas size
    const int2 mainBase = trixelFrameOffset(
        trixelOriginOffsetZ1(canvasSize),
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );

    // Emit the micro-cell's two-pixel diamond region (#1724), not just its
    // origin pixel: roundHalfUp collapses a region's input pixels onto one
    // recovered cell, so a single-pixel write left the resolve ~50% sparse —
    // pinhole casters whose world-placed shadows dithered. The detached store
    // is model-frame (slot = model face axis), so rotate the face into the
    // view frame the same way the position was to pick the region
    // (faceOffset_2x3 is polarity-blind: axis only).
    const int3 viewNormal =
        rotateCardinalZ(faceOutwardNormal6I(slot << 1), cardinalIndex);
    const int viewAxis =
        (viewNormal.x != 0) ? kXFace : ((viewNormal.y != 0) ? kYFace : kZFace);
    const int2 cellBase = mainBase + pos3DtoPos2DIso(viewPos);
    for (int k = 0; k < 2; ++k) {
        const int2 mainPixel = cellBase + faceOffset_2x3(viewAxis, k);
        if (!isInsideCanvas(mainPixel, canvasSize)) {
            continue;
        }
        // Front-most per screen pixel: smallest encoded distance wins (depth
        // dominates the 2 slot bits), exactly like the main canvas atomicMin
        // store.
        const uint idx = uint(mainPixel.y) * uint(canvasSize.x) + uint(mainPixel.x);
        atomic_fetch_min_explicit(&resolveScratch[idx], encoded, memory_order_relaxed);
    }
}

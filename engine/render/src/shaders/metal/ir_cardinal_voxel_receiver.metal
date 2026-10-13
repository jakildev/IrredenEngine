#ifndef IR_CARDINAL_VOXEL_RECEIVER_METAL_INCLUDED
#define IR_CARDINAL_VOXEL_RECEIVER_METAL_INCLUDED

// Requires ir_iso_common and an undilated cardinal voxel pixel, not a shape sample.
inline float3 cardinalVoxelSurfaceCenter(
    int2 pixel, int encoded, int2 trixelCanvasOffsetZ1, float2 frameCanvasOffset,
    int2 voxelRenderOptions, int cardinalIndex
) {
    const int slot = decodeSlot(encoded);
    const int2 iso = trixelCanvasPixelToIsoRel(
        pixel, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
    // Integer micro-origins have even iso parity. Only the Y pair starts odd.
    const int emittedHalf = ((iso.x + iso.y) & 1) ^ (slot == kYFace ? 1 : 0);
    const int2 originIso = iso - faceOffset_2x3(slot, emittedHalf);
    const float3 origin = isoPixelToPos3D(
        originIso.x, originIso.y, float(decodeDepthSingle(encoded)));
    if (voxelRenderOptions.x == 0) {
        const float3 normal = faceOutwardNormal(slot) *
            (decodeFlipSingle(encoded) != 0 ? -1.0 : 1.0);
        return rotateCardinalZInv(origin + 0.5 * normal, cardinalIndex);
    }
    const float density = float(effectiveTrixelSubdivisionScale(voxelRenderOptions));
    const float3 axis = faceOutOfPlaneUnitAxis(slot);
    // Polarity is in the stored plane. Center the micro-square, then the voxel
    // in the view frame before rotating its exact surface point back to world.
    return rotateCardinalZInv(
        (origin + 0.5 * (float3(1.0) - axis)) / density - kVoxelRasterCellAnchor,
        cardinalIndex);
}

#endif

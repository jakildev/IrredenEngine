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

// Intersect the displayed fragment ray with the stored face plane. Rectangular
// storage approximates the silhouette; do not clamp to a micro-square and tear
// an otherwise continuous coplanar receiver into separate shadow samples.
float3 cardinalVoxelSurfacePoint(
    float2 canvasPixel, int2 ownerPixel, int encoded, int2 z1, float2 cameraOffset,
    int2 renderOptions, int cardinalIndex
) {
    const float3 center = cardinalVoxelSurfaceCenter(
        ownerPixel, encoded, z1, cameraOffset, renderOptions, 0);
    const float density = float(effectiveTrixelSubdivisionScale(renderOptions));
    const float2 iso = (canvasPixel - float2(trixelFrameOffset(z1, cameraOffset, renderOptions))
                      - float2(1.0)) / density;
    float3 point = isoPositionToPos3D(iso, 0.0);
    const float3 axis = faceOutOfPlaneUnitAxis(decodeSlot(encoded));
    point += float3(dot(center - point, axis));
    return rotateCardinalZInv(point, cardinalIndex);
}

#endif

#ifndef IR_CARDINAL_VOXEL_RECEIVER_GLSL_INCLUDED
#define IR_CARDINAL_VOXEL_RECEIVER_GLSL_INCLUDED

// Requires ir_iso_common and an undilated cardinal voxel pixel, not a shape sample.
vec3 cardinalVoxelSurfaceCenter(
    ivec2 pixel, int encoded, ivec2 trixelCanvasOffsetZ1, vec2 frameCanvasOffset,
    ivec2 voxelRenderOptions, int cardinalIndex
) {
    const int slot = decodeSlot(encoded);
    const ivec2 iso = trixelCanvasPixelToIsoRel(
        pixel, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
    // Integer micro-origins have even iso parity. Only the Y pair starts odd.
    const int emittedHalf = ((iso.x + iso.y) & 1) ^ (slot == kYFace ? 1 : 0);
    const ivec2 originIso = iso - faceOffset_2x3(slot, emittedHalf);
    const vec3 origin = isoPixelToPos3D(
        originIso.x, originIso.y, float(decodeDepthSingle(encoded)));
    if (voxelRenderOptions.x == 0) {
        const vec3 normal = faceOutwardNormal(slot) *
            (decodeFlipSingle(encoded) != 0 ? -1.0 : 1.0);
        return rotateCardinalZInv(origin + 0.5 * normal, cardinalIndex);
    }
    const float density = float(effectiveTrixelSubdivisionScale(voxelRenderOptions));
    const vec3 axis = faceOutOfPlaneUnitAxis(slot);
    // Polarity is in the stored plane. Center the micro-square, then the voxel
    // in the view frame before rotating its exact surface point back to world.
    return rotateCardinalZInv(
        (origin + 0.5 * (vec3(1.0) - axis)) / density - kVoxelRasterCellAnchor,
        cardinalIndex);
}

#endif

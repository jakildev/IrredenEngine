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

// True when `texel`, one micro-step from the owner in `direction` along an
// in-plane axis, stores the owner face's coplanar continuation: same slot and
// flip, stored depth one step on, cardinal voxel provenance. Every in-plane iso
// step has even parity, so `texel` is the neighbour's half matching the owner's.
inline bool cardinalVoxelCoplanarNeighbour(
    texture2d<int> distances, texture2d<uint> entityIds, int2 texel, int encoded, int direction
) {
    const int2 size = int2(distances.get_width(), distances.get_height());
    if (texel.x < 0 || texel.y < 0 || texel.x >= size.x || texel.y >= size.y) return false;
    const int neighbour = distances.read(uint2(texel)).r;
    return decodeSlot(neighbour) == decodeSlot(encoded) &&
        decodeFlipSingle(neighbour) == decodeFlipSingle(encoded) &&
        decodeDepthSingle(neighbour) == decodeDepthSingle(encoded) + direction &&
        decodeEntityIdCardinalVoxel(entityIds.read(uint2(texel)).rg);
}

// Limits `point` to the owner micro-square along in-plane axis `e` unless the
// micro-square beyond that edge is coplanar. At a concave or convex edge the
// unlimited point lies in or behind the neighbouring voxel and the finite
// shadow query reads it as occluded.
inline float3 cardinalVoxelEdgeLimit(
    float3 point, float3 center, float3 e, float halfExtent, int2 ownerPixel, int encoded,
    texture2d<int> distances, texture2d<uint> entityIds
) {
    const float s = dot(point - center, e);
    if (abs(s) <= halfExtent + 1.0e-4) return point;
    const int direction = s > 0.0 ? 1 : -1;
    const int2 isoStep = pos3DtoPos2DIso(int3(e)) * direction;
    if (cardinalVoxelCoplanarNeighbour(
            distances, entityIds, ownerPixel + isoStep, encoded, direction)) return point;
    return point + e * (float(direction) * halfExtent - s);
}

// Intersect the displayed fragment ray with the stored face plane. Rectangular
// storage approximates the silhouette, so across a coplanar neighbour the point
// keeps extrapolating and a flat run stays one continuous shadow receiver; at
// any other edge it stops at the owner micro-square.
inline float3 cardinalVoxelSurfacePoint(
    float2 canvasPixel, int2 ownerPixel, int encoded, int2 z1, float2 cameraOffset,
    int2 renderOptions, int cardinalIndex, texture2d<int> distances, texture2d<uint> entityIds
) {
    const float3 center = cardinalVoxelSurfaceCenter(
        ownerPixel, encoded, z1, cameraOffset, renderOptions, 0);
    const float density = float(effectiveTrixelSubdivisionScale(renderOptions));
    const float2 iso = (canvasPixel - float2(trixelFrameOffset(z1, cameraOffset, renderOptions))
                      - float2(1.0)) / density;
    float3 point = isoPositionToPos3D(iso, 0.0);
    const int slot = decodeSlot(encoded);
    const float3 axis = faceOutOfPlaneUnitAxis(slot);
    point += float3(dot(center - point, axis));
    float3 eu, ev;
    faceInPlaneUnitAxes(slot, eu, ev);
    point = cardinalVoxelEdgeLimit(
        point, center, eu, 0.5 / density, ownerPixel, encoded, distances, entityIds);
    point = cardinalVoxelEdgeLimit(
        point, center, ev, 0.5 / density, ownerPixel, encoded, distances, entityIds);
    return rotateCardinalZInv(point, cardinalIndex);
}

#endif

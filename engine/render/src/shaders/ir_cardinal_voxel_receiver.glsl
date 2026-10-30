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

// True when `texel`, one micro-step from the owner in `direction` along an
// in-plane axis, stores the owner face's coplanar continuation: same slot and
// flip, stored depth one step on, cardinal voxel provenance. Every in-plane iso
// step has even parity, so `texel` is the neighbour's half matching the owner's.
bool cardinalVoxelCoplanarNeighbour(
    isampler2D distances, usampler2D entityIds, ivec2 texel, int encoded, int direction
) {
    const ivec2 size = textureSize(distances, 0);
    if (texel.x < 0 || texel.y < 0 || texel.x >= size.x || texel.y >= size.y) return false;
    const int neighbour = texelFetch(distances, texel, 0).r;
    return decodeSlot(neighbour) == decodeSlot(encoded) &&
        decodeFlipSingle(neighbour) == decodeFlipSingle(encoded) &&
        decodeDepthSingle(neighbour) == decodeDepthSingle(encoded) + direction &&
        decodeEntityIdCardinalVoxel(texelFetch(entityIds, texel, 0).rg);
}

// Limits `point` to the owner micro-square along in-plane axis `e` unless the
// micro-square beyond that edge is coplanar. At a concave or convex edge the
// unlimited point lies in or behind the neighbouring voxel and the finite
// shadow query reads it as occluded.
vec3 cardinalVoxelEdgeLimit(
    vec3 point, vec3 center, vec3 e, float halfExtent, ivec2 ownerPixel, int encoded,
    isampler2D distances, usampler2D entityIds
) {
    const float s = dot(point - center, e);
    if (abs(s) <= halfExtent + 1.0e-4) return point;
    const int direction = s > 0.0 ? 1 : -1;
    const ivec2 isoStep = pos3DtoPos2DIso(ivec3(e)) * direction;
    if (cardinalVoxelCoplanarNeighbour(
            distances, entityIds, ownerPixel + isoStep, encoded, direction)) return point;
    return point + e * (float(direction) * halfExtent - s);
}

// Intersect the displayed fragment ray with the stored face plane. Rectangular
// storage approximates the silhouette, so across a coplanar neighbour the point
// keeps extrapolating and a flat run stays one continuous shadow receiver; at
// any other edge it stops at the owner micro-square.
vec3 cardinalVoxelSurfacePoint(
    vec2 canvasPixel, ivec2 ownerPixel, int encoded, ivec2 z1, vec2 cameraOffset,
    ivec2 renderOptions, int cardinalIndex, isampler2D distances, usampler2D entityIds
) {
    const vec3 center = cardinalVoxelSurfaceCenter(
        ownerPixel, encoded, z1, cameraOffset, renderOptions, 0);
    const float density = float(effectiveTrixelSubdivisionScale(renderOptions));
    const vec2 iso = (canvasPixel - vec2(trixelFrameOffset(z1, cameraOffset, renderOptions))
                      - vec2(1.0)) / density;
    vec3 point = isoPositionToPos3D(iso, 0.0);
    const int slot = decodeSlot(encoded);
    const vec3 axis = faceOutOfPlaneUnitAxis(slot);
    point += vec3(dot(center - point, axis));
    vec3 eu, ev;
    faceInPlaneUnitAxes(slot, eu, ev);
    point = cardinalVoxelEdgeLimit(
        point, center, eu, 0.5 / density, ownerPixel, encoded, distances, entityIds);
    point = cardinalVoxelEdgeLimit(
        point, center, ev, 0.5 / density, ownerPixel, encoded, distances, entityIds);
    return rotateCardinalZInv(point, cardinalIndex);
}

#endif

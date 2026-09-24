void indexSourceSunFace(vec3 corner, vec3 edgeU, vec3 edgeV) {
    const float determinant = projectedFaceDeterminant(edgeU.xy, edgeV.xy);
    if (abs(determinant) < 0.000001) return;
    uint faceIndex = 0xFFFFFFFFu;
    const vec2 low = min(min(corner.xy, corner.xy + edgeU.xy), min(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    const vec2 high = max(max(corner.xy, corner.xy + edgeU.xy), max(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    for (uint cascade = 0u; cascade < kSourceFaceCascadeCount; ++cascade) {
        const vec2 origin = cascade == 0u ? cascadeOriginUV_0 : cascadeOriginUV_1;
        const vec2 cellSize = (cascade == 0u ? cascadeTexelSize_0 : cascadeTexelSize_1) * float(kSourceFaceTileEdge);
        const ivec2 first = max(ivec2(floor((low - origin) / cellSize)), ivec2(0));
        const ivec2 last = min(ivec2(floor((high - origin) / cellSize)), ivec2(kSourceFaceTilesPerAxis - 1u));
        if (first.x > last.x || first.y > last.y) continue;
        if (faceIndex == 0xFFFFFFFFu) {
            faceIndex = atomicAdd(sunDepthBuf[sourceFaceHeaderIndex(uint(sunDepthBuf.length()))], 1u);
            if (faceIndex < kSourceFaceCapacity) {
                const uint record = kSourceFaceRecordOffset + faceIndex * kSourceFaceRecordWords;
                for (uint axis = 0; axis < 3u; ++axis) {
                    sunDepthBuf[record + axis] = floatBitsToUint(corner[axis]);
                    sunDepthBuf[record + 3u + axis] = floatBitsToUint(edgeU[axis]);
                    sunDepthBuf[record + 6u + axis] = floatBitsToUint(edgeV[axis]);
                }
            }
        }
        for (int y = first.y; y <= last.y; ++y) for (int x = first.x; x <= last.x; ++x) {
            const uint tile = cascade * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis + uint(y) * kSourceFaceTilesPerAxis + uint(x);
            const uint base = sourceFaceTileBase(tile);
            if (faceIndex >= kSourceFaceCapacity) {
                atomicMax(sunDepthBuf[base], kSourceFaceTileCapacity + 1u);
                continue;
            }
            const uint slot = atomicAdd(sunDepthBuf[base], 1u);
            if (slot < kSourceFaceTileCapacity) sunDepthBuf[base + 1u + slot] = faceIndex;
        }
    }
}

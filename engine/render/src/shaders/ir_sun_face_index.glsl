// Cooperative callers provide identical geometry and cascade bounds on every lane.
void indexSourceSunGeometry(vec3 corner, vec3 edgeU, vec3 edgeV, uint type, vec2 low, vec2 high) {
#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
    const uint lane = gl_LocalInvocationID.x;
    const uint stride = 64u;
#endif
    uint faceIndex = 0xFFFFFFFFu;
    for (uint cascade = 0u; cascade < kSourceFaceCascadeCount; ++cascade) {
        const vec2 origin = cascade == 0u ? cascadeOriginUV_0 : cascadeOriginUV_1;
        const vec2 cellSize = (cascade == 0u ? cascadeTexelSize_0 : cascadeTexelSize_1) * float(kSourceFaceTileEdge);
        const ivec2 first = max(ivec2(floor((low - origin) / cellSize)), ivec2(0));
        const ivec2 last = min(ivec2(floor((high - origin) / cellSize)), ivec2(kSourceFaceTilesPerAxis - 1u));
        if (first.x > last.x || first.y > last.y) continue;
        if (faceIndex == 0xFFFFFFFFu) {
#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
            if (lane == 0u) {
#endif
            faceIndex = atomicAdd(sunDepthBuf[sourceFaceHeaderIndex(uint(sunDepthBuf.length()))], 1u);
            if (faceIndex < kSourceFaceCapacity) {
                const uint record = kSourceFaceRecordOffset + faceIndex * kSourceFaceRecordWords;
                sunDepthBuf[record + 9u] = type;
                for (uint axis = 0; axis < 3u; ++axis) {
                    sunDepthBuf[record + axis] = floatBitsToUint(corner[axis]);
                    sunDepthBuf[record + 3u + axis] = floatBitsToUint(edgeU[axis]);
                    sunDepthBuf[record + 6u + axis] = floatBitsToUint(edgeV[axis]);
                }
            }
#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
            sharedFaceIndex = faceIndex;
            }
            barrier();
            faceIndex = sharedFaceIndex;
            // Every lane must consume the shared record before the next face overwrites it.
            barrier();
#endif
        }
#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
        const uint width = uint(last.x - first.x + 1);
        const uint count = width * uint(last.y - first.y + 1);
        for (uint item = lane; item < count; item += stride) {
            const int x = first.x + int(item % width);
            const int y = first.y + int(item / width);
#else
        for (int y = first.y; y <= last.y; ++y) for (int x = first.x; x <= last.x; ++x) {
#endif
            const uint tile = cascade * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis + uint(y) * kSourceFaceTilesPerAxis + uint(x);
            const uint base = sourceFaceTileBase(tile);
            if (faceIndex >= kSourceFaceCapacity) {
                atomicMax(sunDepthBuf[base], kSourceFaceTileQueryCapacity + 1u);
                continue;
            }
            const uint slot = atomicAdd(sunDepthBuf[base], 1u);
            if (slot < kSourceFaceTileCapacity) {
                sunDepthBuf[base + 1u + slot] = faceIndex;
            } else if (slot < kSourceFaceTileQueryCapacity) {
                const uint spill = atomicAdd(sunDepthBuf[sourceFaceSpillCounterIndex(uint(sunDepthBuf.length()))], 1u);
                if (spill < kSourceFaceSpillCapacity) {
                    const uint node = kSourceFaceSpillNodeOffset + spill * kSourceFaceSpillNodeWords;
                    sunDepthBuf[node] = faceIndex;
                    sunDepthBuf[node + 1u] = atomicExchange(sunDepthBuf[kSourceFaceSpillHeadOffset + tile], spill);
                } else {
                    atomicMax(sunDepthBuf[base], kSourceFaceTileQueryCapacity + 1u);
                }
            }
        }
    }
}

void indexSourceSunFace(vec3 corner, vec3 edgeU, vec3 edgeV) {
    if (abs(projectedFaceDeterminant(edgeU.xy, edgeV.xy)) < 0.000001) return;
    const vec2 low = min(min(corner.xy, corner.xy + edgeU.xy), min(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    const vec2 high = max(max(corner.xy, corner.xy + edgeU.xy), max(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    indexSourceSunGeometry(corner, edgeU, edgeV, 0u, low, high);
}

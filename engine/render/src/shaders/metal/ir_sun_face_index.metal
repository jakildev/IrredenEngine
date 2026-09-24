inline void indexSourceSunFace(device atomic_uint* sunDepthBuf, float3 corner, float3 edgeU, float3 edgeV, constant FrameDataSun& sunFrame) {
    const float determinant = projectedFaceDeterminant(edgeU.xy, edgeV.xy);
    if (abs(determinant) < 0.000001) return;
    uint faceIndex = 0xFFFFFFFFu;
    const float2 low = min(min(corner.xy, corner.xy + edgeU.xy), min(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    const float2 high = max(max(corner.xy, corner.xy + edgeU.xy), max(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    for (uint cascade = 0u; cascade < kSourceFaceCascadeCount; ++cascade) {
        const float2 origin = cascade == 0u ? sunFrame.cascadeOriginUV_0 : sunFrame.cascadeOriginUV_1;
        const float2 cellSize = (cascade == 0u ? sunFrame.cascadeTexelSize_0 : sunFrame.cascadeTexelSize_1) * float(kSourceFaceTileEdge);
        const int2 first = max(int2(floor((low - origin) / cellSize)), int2(0));
        const int2 last = min(int2(floor((high - origin) / cellSize)), int2(kSourceFaceTilesPerAxis - 1u));
        if (first.x > last.x || first.y > last.y) continue;
        if (faceIndex == 0xFFFFFFFFu) {
            faceIndex = atomic_fetch_add_explicit(&sunDepthBuf[kSourceFaceHeaderOffset], 1u, memory_order_relaxed);
            if (faceIndex < kSourceFaceCapacity) {
                const uint record = kSourceFaceRecordOffset + faceIndex * kSourceFaceRecordWords;
                for (uint axis = 0; axis < 3u; ++axis) {
                    atomic_store_explicit(&sunDepthBuf[record + axis], as_type<uint>(corner[axis]), memory_order_relaxed);
                    atomic_store_explicit(&sunDepthBuf[record + 3u + axis], as_type<uint>(edgeU[axis]), memory_order_relaxed);
                    atomic_store_explicit(&sunDepthBuf[record + 6u + axis], as_type<uint>(edgeV[axis]), memory_order_relaxed);
                }
            }
        }
        for (int y = first.y; y <= last.y; ++y) for (int x = first.x; x <= last.x; ++x) {
            const uint tile = cascade * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis + uint(y) * kSourceFaceTilesPerAxis + uint(x);
            const uint base = sourceFaceTileBase(tile);
            if (faceIndex >= kSourceFaceCapacity) {
                atomic_fetch_max_explicit(&sunDepthBuf[base], kSourceFaceTileCapacity + 1u, memory_order_relaxed);
                continue;
            }
            const uint slot = atomic_fetch_add_explicit(&sunDepthBuf[base], 1u, memory_order_relaxed);
            if (slot < kSourceFaceTileCapacity) atomic_store_explicit(&sunDepthBuf[base + 1u + slot], faceIndex, memory_order_relaxed);
        }
    }
}

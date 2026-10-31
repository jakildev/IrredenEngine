#include "ir_sun_face_bounds.metal"

// Cooperative callers provide identical geometry and cascade bounds on every lane.
#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
inline void indexSourceSunGeometry(device atomic_uint* sunDepthBuf, float3 corner, float3 edgeU, float3 edgeV, uint type, float2 low, float2 high, constant FrameDataSun& sunFrame, uint lane, threadgroup uint& sharedFaceIndex) {
#else
inline void indexSourceSunGeometry(device atomic_uint* sunDepthBuf, float3 corner, float3 edgeU, float3 edgeV, uint type, float2 low, float2 high, constant FrameDataSun& sunFrame) {
#endif
#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
    const uint stride = 64u;
#endif
    const uint capacity = atomic_load_explicit(&sunDepthBuf[kSourceFaceCapacityOffset], memory_order_relaxed);
    uint faceIndex = 0xFFFFFFFFu;
    for (uint cascade = 0u; cascade < kSourceFaceCascadeCount; ++cascade) {
        const float2 origin = cascade == 0u ? sunFrame.cascadeOriginUV_0 : sunFrame.cascadeOriginUV_1;
        const float2 cellSize = (cascade == 0u ? sunFrame.cascadeTexelSize_0 : sunFrame.cascadeTexelSize_1) * float(kSourceFaceTileEdge);
        const bool fullCascade = type == 0u && !sourceFaceCascadeArithmeticSafe(origin, cellSize);
        const float2 tileX = fullCascade ? float2(0.0, float(kSourceFaceTilesPerAxis - 1u))
            : sourceFaceTileInterval(low.x, high.x, origin.x, cellSize.x);
        const float2 tileY = fullCascade ? float2(0.0, float(kSourceFaceTilesPerAxis - 1u))
            : sourceFaceTileInterval(low.y, high.y, origin.y, cellSize.y);
        const int2 first = int2(clamp(floor(float2(tileX.x, tileY.x)),
            float2(0.0), float2(float(kSourceFaceTilesPerAxis))));
        const int2 last = int2(clamp(floor(float2(tileX.y, tileY.y)),
            float2(-1.0), float2(float(kSourceFaceTilesPerAxis - 1u))));
        if (first.x > last.x || first.y > last.y) continue;
        if (faceIndex == 0xFFFFFFFFu) {
#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
            if (lane == 0u) {
#endif
            faceIndex = atomic_fetch_add_explicit(&sunDepthBuf[kSourceFaceHeaderOffset], 1u, memory_order_relaxed);
            if (faceIndex == capacity) atomic_store_explicit(&sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)], 1u, memory_order_relaxed);
            if (faceIndex < capacity) {
                const uint record = kSourceFaceRecordOffset + faceIndex * kSourceFaceRecordWords;
                atomic_store_explicit(&sunDepthBuf[record + 9u], type, memory_order_relaxed);
                for (uint axis = 0; axis < 3u; ++axis) {
                    atomic_store_explicit(&sunDepthBuf[record + axis], as_type<uint>(corner[axis]), memory_order_relaxed);
                    atomic_store_explicit(&sunDepthBuf[record + 3u + axis], as_type<uint>(edgeU[axis]), memory_order_relaxed);
                    atomic_store_explicit(&sunDepthBuf[record + 6u + axis], as_type<uint>(edgeV[axis]), memory_order_relaxed);
                }
            }
#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
            sharedFaceIndex = faceIndex;
            }
            threadgroup_barrier(mem_flags::mem_threadgroup);
            faceIndex = sharedFaceIndex;
            // Every lane must consume the shared record before the next face overwrites it.
            threadgroup_barrier(mem_flags::mem_threadgroup);
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
            if (faceIndex >= capacity) {
                atomic_fetch_or_explicit(&sunDepthBuf[base], kSourceFaceTileRecordLost, memory_order_relaxed);
                continue;
            }
            const uint slot = atomic_fetch_add_explicit(&sunDepthBuf[base], 1u, memory_order_relaxed);
            if (slot == kSourceFaceTileQueryCapacity) atomic_store_explicit(&sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)], 1u, memory_order_relaxed);
            if (slot < kSourceFaceTileCapacity) {
                atomic_store_explicit(&sunDepthBuf[base + 1u + slot], faceIndex, memory_order_relaxed);
            } else if (slot < kSourceFaceTileQueryCapacity) {
                const uint spill = atomic_fetch_add_explicit(&sunDepthBuf[sourceFaceSpillCounterIndex(capacity)], 1u, memory_order_relaxed);
                if (spill == kSourceFaceSpillCapacity) atomic_store_explicit(&sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)], 1u, memory_order_relaxed);
                if (spill < kSourceFaceSpillCapacity) {
                    const uint node = sourceFaceSpillNodeIndex(capacity, spill);
                    atomic_store_explicit(&sunDepthBuf[node], faceIndex, memory_order_relaxed);
                    const uint previous = atomic_exchange_explicit(&sunDepthBuf[sourceFaceSpillHeadIndex(capacity, tile)], spill, memory_order_relaxed);
                    atomic_store_explicit(&sunDepthBuf[node + 1u], previous, memory_order_relaxed);
                } else {
                    atomic_fetch_max_explicit(&sunDepthBuf[base], kSourceFaceTileQueryCapacity + 1u, memory_order_relaxed);
                }
            }
        }
    }
}

#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
inline void indexSourceSunFace(device atomic_uint* sunDepthBuf, float3 corner, float3 edgeU, float3 edgeV, constant FrameDataSun& sunFrame, uint lane, threadgroup uint& sharedFaceIndex) {
#else
inline void indexSourceSunFace(device atomic_uint* sunDepthBuf, float3 corner, float3 edgeU, float3 edgeV, constant FrameDataSun& sunFrame) {
#endif
    const float determinant = projectedFaceDeterminant(edgeU.xy, edgeV.xy);
    if (sourceFaceDefinitelyDegenerate(edgeU.xy, edgeV.xy, determinant)) return;
    float2 low, high;
    if (!sourceFaceIndexBounds(corner.xy, edgeU.xy, edgeV.xy, determinant, low, high)) {
        low = float2(-3.4028234663852886e38);
        high = float2(3.4028234663852886e38);
    }
#ifdef IR_SUN_FACE_INDEX_COOPERATIVE
    indexSourceSunGeometry(sunDepthBuf, corner, edgeU, edgeV, 0u, low, high, sunFrame, lane, sharedFaceIndex);
#else
    indexSourceSunGeometry(sunDepthBuf, corner, edgeU, edgeV, 0u, low, high, sunFrame);
#endif
}

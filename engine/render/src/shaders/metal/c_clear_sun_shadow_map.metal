#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

// Mirrors shaders/c_clear_sun_shadow_map.glsl.

#include "ir_sun_projection.metal"
#include "ir_sun_face_query_layout.metal"

constant int kTotalTexels = int(kSourceFaceFallbackOffset);

kernel void c_clear_sun_shadow_map(
    device atomic_uint *sunDepthBuf [[buffer(28)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 gid = int2(globalId.xy);
    int linearIdx = gid.y * kSunShadowMapDim + gid.x;
    if (linearIdx >= kTotalTexels) {
        return;
    }
    const uint capacity = atomic_load_explicit(&sunDepthBuf[kSourceFaceCapacityOffset], memory_order_relaxed);
    // The capacity word beside the header belongs to the allocator and survives the clear.
    if (linearIdx == 0) atomic_store_explicit(&sunDepthBuf[kSourceFaceHeaderOffset], 0u, memory_order_relaxed);
    if (linearIdx == 0) atomic_store_explicit(&sunDepthBuf[sourceFaceSpillCounterIndex(capacity)], 0u, memory_order_relaxed);
    if (linearIdx == 0) atomic_store_explicit(&sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)], 0u, memory_order_relaxed);
    if (linearIdx == 0) atomic_store_explicit(&sunDepthBuf[sourceFaceHierarchyReadyIndex(capacity)], 0u, memory_order_relaxed);
    if (uint(linearIdx) < kSourceFaceHierarchyOrderBuckets)
        atomic_store_explicit(&sunDepthBuf[sourceFaceHierarchyOrderIndex(capacity) + uint(linearIdx)], 0u, memory_order_relaxed);
    if (uint(linearIdx) < kSourceFaceTileCount) {
        atomic_store_explicit(&sunDepthBuf[sourceFaceTileBase(uint(linearIdx))], 0u, memory_order_relaxed);
        atomic_store_explicit(&sunDepthBuf[sourceFaceSpillHeadIndex(capacity, uint(linearIdx))], 0xFFFFFFFFu, memory_order_relaxed);
    }
    atomic_store_explicit(&sunDepthBuf[linearIdx + kSourceFaceFallbackOffset], 0xFFFFFFFFu, memory_order_relaxed);
    atomic_store_explicit(
        &sunDepthBuf[linearIdx],
        0xFFFFFFFFu,
        memory_order_relaxed
    );
}

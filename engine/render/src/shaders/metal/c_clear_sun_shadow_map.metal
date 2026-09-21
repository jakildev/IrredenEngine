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
    if (linearIdx == 0) atomic_store_explicit(&sunDepthBuf[kSourceFaceHeaderOffset], 0u, memory_order_relaxed);
    if (uint(linearIdx) < kSourceFaceTileCount) atomic_store_explicit(&sunDepthBuf[sourceFaceTileBase(uint(linearIdx))], 0u, memory_order_relaxed);
    atomic_store_explicit(&sunDepthBuf[linearIdx + kSourceFaceFallbackOffset], 0xFFFFFFFFu, memory_order_relaxed);
    atomic_store_explicit(
        &sunDepthBuf[linearIdx],
        0xFFFFFFFFu,
        memory_order_relaxed
    );
}

#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

// Mirrors shaders/c_clear_sun_shadow_map.glsl.

constant int kSunShadowMapDim = 1024;
constant int kSunShadowCascadeCount = 2;
constant int kTotalTexels = kSunShadowMapDim * kSunShadowMapDim * kSunShadowCascadeCount;

kernel void c_clear_sun_shadow_map(
    device atomic_uint *sunDepthBuf [[buffer(28)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 gid = int2(globalId.xy);
    int linearIdx = gid.y * kSunShadowMapDim + gid.x;
    if (linearIdx >= kTotalTexels) {
        return;
    }
    atomic_store_explicit(
        &sunDepthBuf[linearIdx],
        0xFFFFFFFFu,
        memory_order_relaxed
    );
}

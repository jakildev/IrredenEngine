#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_resolve_per_axis_blit.glsl. Materializes the scatter
// pass's scratch buffer into the resolve R32I texture BAKE_SUN_SHADOW_MAP
// reads, then resets each scratch slot to the empty sentinel so the next
// frame needs no separate clear dispatch (#1435). Runs after a barrier, so a
// plain device pointer (non-atomic) read/write is safe here.

constant int kEmptyDistanceEncoded = 65535;

kernel void c_resolve_per_axis_blit(
    device int* resolveScratch [[buffer(28)]],
    texture2d<int, access::write> resolveDepth [[texture(1)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 pixel = int2(globalId.xy);
    const int2 size = int2(int(resolveDepth.get_width()), int(resolveDepth.get_height()));
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }
    const uint idx = uint(pixel.y) * uint(size.x) + uint(pixel.x);
    resolveDepth.write(int4(resolveScratch[idx], 0, 0, 0), uint2(pixel));
    resolveScratch[idx] = kEmptyDistanceEncoded;
}

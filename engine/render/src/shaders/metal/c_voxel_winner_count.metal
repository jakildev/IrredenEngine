#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

kernel void c_voxel_winner_count(
    texture2d<int, access::read> canvasDistances [[texture(1)]],
    const device uint* winnerIds [[buffer(28)]],
    device atomic_uint* seenWords [[buffer(24)]],
    device atomic_uint* counts [[buffer(26)]],
    uint2 pixel [[thread_position_in_grid]]
) {
    const uint2 size = uint2(canvasDistances.get_width(), canvasDistances.get_height());
    if (any(pixel >= size)) return;
    const uint winner = winnerIds[pixel.y * size.x + pixel.x];
    if (winner == 0xFFFFFFFFu) return;
    const uint bit = 1u << (winner & 31u);
    if ((atomic_fetch_or_explicit(&seenWords[winner >> 5u], bit, memory_order_relaxed) & bit) == 0u) {
        atomic_fetch_add_explicit(&counts[6], 1u, memory_order_relaxed);
    }
}

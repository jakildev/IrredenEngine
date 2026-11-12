#include <metal_stdlib>
#include <metal_atomic>
#include "ir_per_axis_cell_dispatch.metal"
using namespace metal;

// Mirror of shaders/c_per_axis_cell_clear.glsl. Stage 1's per-axis distance
// taps land in the R32I image-atomic scratch and stage 2 copies each settled
// winner into the texture, so a cell the compaction lists (non-empty texture)
// is also the only kind of cell whose scratch word left the sentinel. Both
// copies are reset here: the scratch is bound at kMetalImageAtomicScratchSlot
// by the distance texture's image bind, and its words are laid out like the
// texture's texels, so the list's linear index addresses both.

// Per-axis canvas empty sentinel — mirror of the GLSL.
constant int kEmptyDistanceEncoded = 0x7FFFFFFF;

kernel void c_per_axis_cell_clear(
    texture2d<float, access::write> perAxisColors [[texture(0)]],
    texture2d<int, access::write> perAxisDistances [[texture(1)]],
    texture2d<uint, access::write> perAxisEntityIds [[texture(2)]],
    device atomic_int* distanceScratch [[buffer(16)]],
    const device uint* compactedCells [[buffer(25)]],
    const device uint* cellDrawArgs [[buffer(26)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint localIndex [[thread_index_in_threadgroup]],
    uint3 numGroups [[threadgroups_per_grid]]
) {
    const uint idx = perAxisCellInvocationIndex(groupId.x, groupId.y, numGroups.x, localIndex);
    if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
        return;
    }
    const uint linearCell = compactedCells[idx];
    const uint2 cell = uint2(perAxisCellPixel(linearCell, int(perAxisDistances.get_width())));
    perAxisColors.write(float4(0.0), cell);
    perAxisDistances.write(int4(kEmptyDistanceEncoded, 0, 0, 0), cell);
    perAxisEntityIds.write(uint4(0u), cell);
    atomic_store_explicit(&distanceScratch[linearCell], kEmptyDistanceEncoded, memory_order_relaxed);
}

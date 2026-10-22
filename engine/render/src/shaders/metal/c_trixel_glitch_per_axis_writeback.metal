#include "ir_iso_common.metal"

constant int kTrixelGlitchTileSize = 16;

kernel void c_trixel_glitch_per_axis_writeback(
    device atomic_uint* tileFlags [[buffer(27)]],
    const device uint* tileList [[buffer(28)]],
    texture2d<float, access::write> liveColors [[texture(0)]],
    texture2d<uint, access::read> liveEntityIds [[texture(2)]],
    texture2d<float, access::read> scratchColors [[texture(5)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localPosition [[thread_position_in_threadgroup]],
    uint localIndex [[thread_index_in_threadgroup]]
) {
    int2 size = int2(int(liveColors.get_width()), int(liveColors.get_height()));
    int2 tileGrid = (size + int2(kTrixelGlitchTileSize - 1)) / kTrixelGlitchTileSize;
    uint tile = tileList[groupId.x];
    int2 tileOrigin = int2(int(tile % uint(tileGrid.x)), int(tile / uint(tileGrid.x))) *
                      kTrixelGlitchTileSize;
    int2 p = tileOrigin + int2(localPosition.xy);
    if (all(p < size)) {
        uint2 id = decodeEntityId(liveEntityIds.read(uint2(p)).xy);
        if (any(id != uint2(0u))) {
            liveColors.write(scratchColors.read(uint2(p)), uint2(p));
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (localIndex == 0u) {
        atomic_store_explicit(&tileFlags[tile], 0u, memory_order_relaxed);
    }
}

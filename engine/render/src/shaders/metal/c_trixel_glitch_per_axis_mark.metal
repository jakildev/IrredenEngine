#include "ir_iso_common.metal"
#include "ir_glitch_common.metal"
#include "ir_per_axis_cell_dispatch.metal"

constant int kTrixelGlitchTileSize = 16;

static int trixelGlitchSubjectSlot(const device uint4* words, uint2 id) {
    if (all(id == uint2(0u))) {
        return -1;
    }
    uint mask = words[1].x;
    uint entry = hash3(id.x, id.y, kGlitchSubjectTableTag) & mask;
    for (uint probe = 0u; probe <= mask; ++probe) {
        uint4 word = words[words[1].y + entry];
        if (all(word.xy == uint2(0u))) {
            return -1;
        }
        if (all(word.xy == id)) {
            return int(word.z);
        }
        entry = (entry + 1u) & mask;
    }
    return -1;
}

static int trixelGlitchRecordSpill(const device uint4* words, int slot) {
    uint base = words[1].z + uint(slot) * kGlitchSubjectRecordWords;
    return int(words[base].w & 0xFFu);
}

kernel void c_trixel_glitch_per_axis_mark(
    const device uint4* subjectWords [[buffer(8)]],
    const device uint* compactedCells [[buffer(25)]],
    const device uint* cellDrawArgs [[buffer(26)]],
    device atomic_uint* tileFlags [[buffer(27)]],
    device uint* tileList [[buffer(28)]],
    device atomic_uint* tileArgs [[buffer(29)]],
    texture2d<uint, access::read> liveEntityIds [[texture(2)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint localIndex [[thread_index_in_threadgroup]],
    uint3 numGroups [[threadgroups_per_grid]]
) {
    uint idx = perAxisCellInvocationIndex(groupId.x, groupId.y, numGroups.x, localIndex);
    if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
        return;
    }
    if (idx == 0u) {
        atomic_store_explicit(&tileArgs[1], 1u, memory_order_relaxed);
        atomic_store_explicit(&tileArgs[2], 1u, memory_order_relaxed);
    }
    int2 size = int2(int(liveEntityIds.get_width()), int(liveEntityIds.get_height()));
    int2 p = perAxisCellPixel(compactedCells[idx], size.x);
    uint2 id = decodeEntityId(liveEntityIds.read(uint2(p)).xy);
    int slot = trixelGlitchSubjectSlot(subjectWords, id);
    if (slot < 0) {
        return;
    }
    int reach = 2 * trixelGlitchRecordSpill(subjectWords, slot);
    int2 tileGrid = (size + int2(kTrixelGlitchTileSize - 1)) / kTrixelGlitchTileSize;
    int2 first = max(p - int2(reach), int2(0)) / kTrixelGlitchTileSize;
    int2 last = min(p + int2(reach), size - int2(1)) / kTrixelGlitchTileSize;
    for (int ty = first.y; ty <= last.y; ++ty) {
        for (int tx = first.x; tx <= last.x; ++tx) {
            uint tile = uint(ty * tileGrid.x + tx);
            uint previous = atomic_exchange_explicit(
                &tileFlags[tile], 1u, memory_order_relaxed
            );
            if (previous == 0u) {
                uint listIndex = atomic_fetch_add_explicit(
                    &tileArgs[0], 1u, memory_order_relaxed
                );
                tileList[listIndex] = tile;
            }
        }
    }
}

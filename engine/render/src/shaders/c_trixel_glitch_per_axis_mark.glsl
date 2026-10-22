#version 450 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_glitch_common.glsl"
#include "ir_per_axis_cell_dispatch.glsl"

layout(std430, binding = 8) readonly buffer TrixelGlitchSubjects {
    uvec4 subjectWords[];
};
layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};
layout(std430, binding = 26) readonly buffer PerAxisCellIndirect {
    uint cellDrawArgs[];
};
layout(std430, binding = 27) buffer TrixelGlitchTileFlags {
    uint tileFlags[];
};
layout(std430, binding = 28) writeonly buffer TrixelGlitchTileList {
    uint tileList[];
};
layout(std430, binding = 29) buffer TrixelGlitchTileArgs {
    uint tileArgs[];
};

layout(rg32ui, binding = 2) readonly uniform uimage2D liveEntityIds;

const int kTileSize = 16;

int subjectSlot(uvec2 id) {
    if (all(equal(id, uvec2(0u)))) {
        return -1;
    }
    uint mask = subjectWords[1].x;
    uint entry = hash3(id.x, id.y, kGlitchSubjectTableTag) & mask;
    for (uint probe = 0u; probe <= mask; ++probe) {
        uvec4 word = subjectWords[subjectWords[1].y + entry];
        if (all(equal(word.xy, uvec2(0u)))) {
            return -1;
        }
        if (all(equal(word.xy, id))) {
            return int(word.z);
        }
        entry = (entry + 1u) & mask;
    }
    return -1;
}

int recordSpill(int slot) {
    uint base = subjectWords[1].z + uint(slot) * kGlitchSubjectRecordWords;
    return int(subjectWords[base].w & 0xFFu);
}

void main() {
    uint idx = perAxisCellInvocationIndex(
        gl_WorkGroupID.x, gl_WorkGroupID.y, gl_NumWorkGroups.x, gl_LocalInvocationIndex
    );
    if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
        return;
    }
    if (idx == 0u) {
        tileArgs[1] = 1u;
        tileArgs[2] = 1u;
    }
    ivec2 size = imageSize(liveEntityIds);
    ivec2 p = perAxisCellPixel(compactedCells[idx], size.x);
    int slot = subjectSlot(decodeEntityId(imageLoad(liveEntityIds, p).xy));
    if (slot < 0) {
        return;
    }
    int reach = 2 * recordSpill(slot);
    ivec2 tileGrid = (size + ivec2(kTileSize - 1)) / kTileSize;
    ivec2 first = max(p - ivec2(reach), ivec2(0)) / kTileSize;
    ivec2 last = min(p + ivec2(reach), size - ivec2(1)) / kTileSize;
    for (int ty = first.y; ty <= last.y; ++ty) {
        for (int tx = first.x; tx <= last.x; ++tx) {
            uint tile = uint(ty * tileGrid.x + tx);
            if (atomicExchange(tileFlags[tile], 1u) == 0u) {
                uint listIndex = atomicAdd(tileArgs[0], 1u);
                tileList[listIndex] = tile;
            }
        }
    }
}

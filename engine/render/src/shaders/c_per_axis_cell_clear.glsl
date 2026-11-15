#version 450 core

#include "ir_per_axis_cell_dispatch.glsl"

// Per-axis store reset over the previous rotating frame's occupied-cell list.
//
// Every write to a per-axis store lands at a cell the compaction lists: the
// distance at a cell that turns non-empty, and the colour and entity id only
// where the distance is non-empty (stage 2, the relight, the fog pass and the
// glitch writeback all gate on that). Writing the empty state at each listed
// cell therefore leaves the store equal to a whole clear. Dispatched once per
// axis, before that frame's compaction resets the list.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Per-axis canvas empty sentinel — mirror of c_per_axis_cell_compact.glsl.
const int kEmptyDistanceEncoded = 0x7FFFFFFF;

layout(rgba8, binding = 0) writeonly uniform image2D perAxisColors;
layout(r32i, binding = 1) writeonly uniform iimage2D perAxisDistances;
layout(rg32ui, binding = 2) writeonly uniform uimage2D perAxisEntityIds;

layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};
layout(std430, binding = 26) readonly buffer PerAxisCellIndirect {
    uint cellDrawArgs[];
};

void main() {
    const uint idx = perAxisCellInvocationIndex(
        gl_WorkGroupID.x, gl_WorkGroupID.y, gl_NumWorkGroups.x, gl_LocalInvocationIndex
    );
    if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
        return;
    }
    const ivec2 cell = perAxisCellPixel(compactedCells[idx], imageSize(perAxisDistances).x);
    imageStore(perAxisColors, cell, vec4(0.0));
    imageStore(perAxisDistances, cell, ivec4(kEmptyDistanceEncoded, 0, 0, 0));
    imageStore(perAxisEntityIds, cell, uvec4(0u));
}

#include <metal_stdlib>
using namespace metal;

// Mirror of shaders/c_per_axis_cell_finalize.glsl (#2256). Derives each per-axis
// canvas's compute-indirect dispatch dims from the occupied count the compaction
// wrote, so the AO / sun-shadow / lighting / resolve stages indirect-dispatch
// over only occupied cells. Split out of the compaction to keep that hot
// full-grid scan barrier-free.
//
// Dispatched as 3 threadgroups of 1 thread each — the default 1×1×1 threadgroup,
// so this kernel needs NO entry in threadgroupSizeForFunctionName. drawArgs is
// bound via bindBase (the whole indirect buffer); each thread owns one axis's
// 256-byte region (no atomics — one writer per region).
constant uint kStrideUints = 64u;             // kPerAxisCellIndirectStrideBytes / 4
constant uint kDispatchArgsBaseUint = 8u;     // kPerAxisCellDispatchArgsOffsetBytes / 4
constant uint kPerAxisCellComputeTile = 256u; // kPerAxisCellComputeTile (16×16 threads)
// Cap numGroupsX and spill the remainder into numGroupsY (mirror of the GLSL
// twin) — matching CPU voxelDispatchGridForCount() and GPU writeDispatchDims().
// Uncapped, >kPerAxisCellComputeTile × 65535 occupied cells overflows the X
// dimension. Consumers recover the flat group index as
// groupId.x + groupId.y * threadgroups_per_grid.x.
constant uint kMaxDispatchGroupsX = 1024u;

kernel void c_per_axis_cell_finalize(
    device uint* drawArgs [[buffer(26)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const uint axis = globalId.x;
    if (axis >= 3u) {
        return;
    }
    const uint base = axis * kStrideUints;
    const uint count = drawArgs[base + 1u]; // instanceCount
    // divCeil(count, tile) workgroups, folded into a capped 2-D grid. An empty
    // axis (count 0 → groups 0) yields groupsX 1, numGroupsY 0 → a no-op dispatch.
    const uint groups = (count + kPerAxisCellComputeTile - 1u) / kPerAxisCellComputeTile;
    const uint groupsX = max(min(groups, kMaxDispatchGroupsX), 1u);
    drawArgs[base + kDispatchArgsBaseUint + 0u] = groupsX;
    drawArgs[base + kDispatchArgsBaseUint + 1u] = (groups + groupsX - 1u) / groupsX;
    drawArgs[base + kDispatchArgsBaseUint + 2u] = 1u;
    drawArgs[base + kDispatchArgsBaseUint + 3u] = count; // visibleCount
}

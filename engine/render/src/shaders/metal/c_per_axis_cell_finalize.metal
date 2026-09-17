#include <metal_stdlib>
using namespace metal;

// Mirror of shaders/c_per_axis_cell_finalize.glsl. Split out of the
// compaction to keep that hot full-grid scan barrier-free.
//
// Dispatched as 3 threadgroups of 1 thread each — one thread per axis, registered
// explicitly as (1,1,1) in threadgroupSizeForFunctionName. The entry is required
// even though it equals the fallback: cmake/run_metal_kernel_registry_check.cmake
// fails on any c_*.metal kernel in this directory without one. drawArgs is
// bound via bindBase (the whole indirect buffer); each thread owns one axis's
// 256-byte region (no atomics — one writer per region).
constant uint kStrideUints = 64u;             // kPerAxisCellIndirectStrideBytes / 4
constant uint kDispatchArgsBaseUint = 8u;     // kPerAxisCellDispatchArgsOffsetBytes / 4
constant uint kPerAxisCellComputeTile = 256u; // kPerAxisCellComputeTile (16×16 threads)
// numGroupsX is capped with the remainder spilled into numGroupsY, matching
// CPU voxelDispatchGridForCount() and GPU writeDispatchDims(): uncapped,
// >kPerAxisCellComputeTile × 65535 occupied cells overflows the X dimension.
// Consumers recover the flat group index as
// groupId.x + groupId.y * threadgroups_per_grid.x.
constant uint kMaxDispatchGroupsX = 1024u;

kernel void c_per_axis_cell_finalize(
    device uint* drawArgs [[buffer(26)]],
    const device uint* overflowControl [[buffer(28)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const uint axis = globalId.x;
    if (axis >= 3u) {
        return;
    }
    if (axis == 0u) {
        // The overflow lighting kernel uses 64 threads and the same 2-D flattening.
        // Its arguments occupy bytes 64..79 of axis zero's indirect region.
        const uint overflowCount = overflowControl[1u];
        const uint overflowGroups = (overflowCount + 63u) / 64u;
        const uint overflowGroupsX = max(min(overflowGroups, kMaxDispatchGroupsX), 1u);
        drawArgs[16u] = overflowGroupsX;
        drawArgs[17u] = (overflowGroups + overflowGroupsX - 1u) / overflowGroupsX;
        drawArgs[18u] = 1u;
        drawArgs[19u] = overflowCount;
    }
    const uint base = axis * kStrideUints;
    const uint count = drawArgs[base + 1u]; // instanceCount
    // An empty axis (count 0 → groups 0) yields groupsX 1, numGroupsY 0 — a
    // no-op dispatch.
    const uint groups = (count + kPerAxisCellComputeTile - 1u) / kPerAxisCellComputeTile;
    const uint groupsX = max(min(groups, kMaxDispatchGroupsX), 1u);
    drawArgs[base + kDispatchArgsBaseUint + 0u] = groupsX;
    drawArgs[base + kDispatchArgsBaseUint + 1u] = (groups + groupsX - 1u) / groupsX;
    drawArgs[base + kDispatchArgsBaseUint + 2u] = 1u;
    drawArgs[base + kDispatchArgsBaseUint + 3u] = count; // visibleCount
}

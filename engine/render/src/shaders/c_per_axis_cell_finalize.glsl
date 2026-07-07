#version 450 core

// Per-axis compute-indirect dispatch-dim finalize (#2256). After
// c_per_axis_cell_compact scans the FULL per-axis grid and atomic-appends each
// axis's occupied cells (leaving the count in the region's instanceCount slot),
// this cheap 3-thread pass reads that final count and writes the compute-indirect
// dispatch dims the per-axis AO / sun-shadow / lighting / resolve stages dispatch
// over. It is split out of the compaction so that hot full-grid scan stays
// barrier-free: computing the dims in the compaction kernel would need a
// cross-workgroup completion barrier over millions of scan invocations, which
// stalled every workgroup (the #2256 self-regression this fix removes).
//
// Dispatched as 3 workgroups of local_size 1 (one axis each); drawArgs is bound
// via bindBase (the WHOLE indirect buffer), so this thread indexes its axis's
// 256-byte region directly. Region layout mirrors ir_render_types.hpp
// (PerAxisCellDrawCommand + the appended VoxelIndirectDispatchParams block):
//   [1]     = instanceCount (occupied count, written by the compaction)
//   [8..10] = numGroupsX / numGroupsY / numGroupsZ  (written here)
//   [11]    = visibleCount  (written here; the compute kernels' 1-D bound guard)

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

const uint kStrideUints = 64u;             // kPerAxisCellIndirectStrideBytes / 4
const uint kDispatchArgsBaseUint = 8u;     // kPerAxisCellDispatchArgsOffsetBytes / 4
const uint kPerAxisCellComputeTile = 256u; // kPerAxisCellComputeTile (16×16 threads)
// Cap numGroupsX and spill the remainder into numGroupsY — matching the CPU
// voxelDispatchGridForCount() (voxel_dispatch_grid.hpp) and the GPU-authored
// writeDispatchDims() in c_voxel_visibility_compact.glsl, which solve the same
// derive-an-indirect-grid-from-a-count problem. Left uncapped, a per-axis canvas
// with more than kPerAxisCellComputeTile × 65535 (≈16.7M) occupied cells — reachable
// at a large Lua-configured voxel-pool edge — drives numGroupsX past the
// GL_MAX_COMPUTE_WORK_GROUP_COUNT[0] guaranteed minimum of 65535, silently
// dropping/corrupting cells. The four consumer kernels recover the flat group
// index as gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x.
const uint kMaxDispatchGroupsX = 1024u;

layout(std430, binding = 26) buffer PerAxisCellIndirect {
    uint drawArgs[];
};

void main() {
    const uint axis = gl_GlobalInvocationID.x;
    if (axis >= 3u) {
        return;
    }
    const uint base = axis * kStrideUints;
    const uint count = drawArgs[base + 1u]; // instanceCount
    // divCeil(count, tile) workgroups, folded into a capped 2-D grid. An empty
    // axis (count 0 → groups 0) yields groupsX 1, numGroupsY 0 → the per-axis
    // compute stages issue a clean no-op indirect dispatch.
    const uint groups = (count + kPerAxisCellComputeTile - 1u) / kPerAxisCellComputeTile;
    const uint groupsX = max(min(groups, kMaxDispatchGroupsX), 1u);
    drawArgs[base + kDispatchArgsBaseUint + 0u] = groupsX;
    drawArgs[base + kDispatchArgsBaseUint + 1u] = (groups + groupsX - 1u) / groupsX;
    drawArgs[base + kDispatchArgsBaseUint + 2u] = 1u;
    drawArgs[base + kDispatchArgsBaseUint + 3u] = count; // visibleCount
}

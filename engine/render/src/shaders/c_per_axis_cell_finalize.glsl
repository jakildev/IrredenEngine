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
    // numGroupsX = divCeil(count, tile); 0 for an empty axis → the per-axis
    // compute stages issue a clean no-op indirect dispatch.
    drawArgs[base + kDispatchArgsBaseUint + 0u] =
        (count + kPerAxisCellComputeTile - 1u) / kPerAxisCellComputeTile;
    drawArgs[base + kDispatchArgsBaseUint + 1u] = 1u;
    drawArgs[base + kDispatchArgsBaseUint + 2u] = 1u;
    drawArgs[base + kDispatchArgsBaseUint + 3u] = count; // visibleCount
}

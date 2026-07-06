#version 450 core

// Per-axis empty-cell compaction pre-pass (#1961).
//
// The smooth-camera-yaw forward-scatter composite (v_peraxis_scatter) used to
// instance over EVERY cell of the worst-case-sized per-axis canvas
// (size.x*size.y, ~12x the cardinal area), degenerating the mostly-empty cells
// in the vertex shader — paying a full vertex-shader invocation per empty cell.
// This kernel scans one per-axis distance canvas and atomic-appends each
// OCCUPIED cell's linear index into a per-axis SSBO region, plus bumps the
// indirect instanced-draw arg's instance count, so the composite draws only
// occupied cells. Dispatched once per axis (the caller bindRange's this axis's
// region of both SSBOs, so index 0 is the axis base). Cardinal byte-identity is
// structural: the per-axis canvases are only allocated at non-zero residual
// yaw, so this kernel never runs at a cardinal.
//
// Occupied test: the per-axis canvas clears to INT_MAX (#1458 encoding), the
// same sentinel c_resolve_per_axis_screen_depth.glsl tests. This is a SUPERSET
// of the scatter's own `color.a < 0.1` discard (a stored face writes color and
// distance together), so the composite stays byte-identical — the scatter keeps
// its color early-out as the exact authority and any extra cell degenerates.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Per-axis canvas empty sentinel (#1458) — mirror of
// c_resolve_per_axis_screen_depth.glsl's kEmptyDistanceEncoded.
const int kEmptyDistanceEncoded = 0x7FFFFFFF;

// Input: ONE per-axis voxel canvas distance store (face-local, R32I).
layout(r32i, binding = 0) readonly uniform iimage2D perAxisDistances;

// Output: this axis's compacted occupied-cell linear indices (bindRange'd per
// axis, so index 0 is the axis region base). The scatter vertex shader reads
// compactedCells[gl_InstanceID].
layout(std430, binding = 25) writeonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};

// This axis's indirect instanced-draw args (bindRange'd per axis). Flat layout
// matches PerAxisCellDrawCommand / DrawElementsIndirectCommand:
//   [0] = indexCount (quad index count, CPU-reset each frame)
//   [1] = instanceCount (atomic append counter = occupied cells)  ┘ (bytes 0..31)
//   [2..4] = firstIndex / baseVertex / baseInstance = 0
//   [8]  = numGroupsX  ┐ compute-indirect (#2256, bytes 32..63) derived by the
//   [9]  = numGroupsY  │ last-workgroup finalize below so the per-axis COMPUTE
//   [10] = numGroupsZ  │ stages (AO/sun-shadow/lighting/resolve) dispatch only
//   [11] = visibleCount│ over occupied cells instead of the full grid.
//   [12] = completedGroups (cross-group finalize barrier)  ┘
// The CPU resets the block each frame; this kernel bumps instanceCount and, once
// the whole grid is scanned, writes the compute-indirect dispatch grid.
layout(std430, binding = 26) buffer PerAxisCellIndirect {
    uint drawArgs[];
};

// #2256: compute-indirect sub-record indices + the 1-D group size the per-axis
// compute stages iterate the list with (mirrors ir_render_types.hpp constants:
// kPerAxisCellComputeDispatchOffsetUints / kPerAxisCellComputeGroupSize).
const uint kComputeBaseIdx = 8u;
const uint kComputeCompletedIdx = 12u; // kComputeBaseIdx + VoxelIndirectDispatchParams::completedGroups
const uint kComputeGroupSize = 64u;

void main() {
    const ivec2 cell = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(perAxisDistances);

    // Append occupied cells. NO early return — every invocation must reach the
    // finalize barrier below uniformly (out-of-range / empty cells just skip the
    // append). Same convention the scatter recovers (ij = cell % size.x).
    if (cell.x < size.x && cell.y < size.y) {
        const int rawDist = imageLoad(perAxisDistances, cell).x;
        if (rawDist < kEmptyDistanceEncoded) {
            const uint slot = atomicAdd(drawArgs[1], 1u);
            compactedCells[slot] = uint(cell.y * size.x + cell.x);
        }
    }

    // Last-workgroup finalize (#2256): derive the compute-indirect dispatch grid
    // from the final occupied count so the per-axis compute stages walk only
    // occupied cells. Mirrors c_voxel_visibility_compact.glsl's completedGroups
    // pattern; the count read is atomic so the last group sees every append.
    barrier();
    memoryBarrierBuffer();
    if (gl_LocalInvocationIndex == 0u) {
        uint finished = atomicAdd(drawArgs[kComputeCompletedIdx], 1u) + 1u;
        uint totalGroups = gl_NumWorkGroups.x * gl_NumWorkGroups.y;
        if (finished == totalGroups) {
            uint count = atomicAdd(drawArgs[1], 0u);
            drawArgs[kComputeBaseIdx + 0u] =
                max((count + kComputeGroupSize - 1u) / kComputeGroupSize, 1u);
            drawArgs[kComputeBaseIdx + 1u] = 1u;
            drawArgs[kComputeBaseIdx + 2u] = 1u;
            drawArgs[kComputeBaseIdx + 3u] = count;
        }
    }
}

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

// This axis's indirect args (bindRange'd per axis). Flat layout matches
// PerAxisCellDrawCommand / DrawElementsIndirectCommand:
//   [0] = indexCount (quad index count, CPU-reset each frame)
//   [1] = instanceCount (atomic append counter — number of occupied cells)
//   [2..4] = firstIndex / baseVertex / baseInstance = 0
// #2256 also feeds the per-axis COMPUTE stages from this same compacted cell
// list; the compute-indirect dims at [8..11] are derived from the final
// instanceCount by the cheap c_per_axis_cell_finalize pass (a 3-thread dispatch
// after this scan). Keeping the dims OUT of this kernel is deliberate: this
// kernel sweeps the FULL per-axis grid, so a cross-workgroup completion barrier
// here would stall every workgroup on the hot scan — the finalize computes the
// dims off-band instead.
layout(std430, binding = 26) buffer PerAxisCellIndirect {
    uint drawArgs[];
};

void main() {
    const ivec2 cell = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(perAxisDistances);
    if (cell.x >= size.x || cell.y >= size.y) {
        return;
    }

    const int rawDist = imageLoad(perAxisDistances, cell).x;
    if (rawDist >= kEmptyDistanceEncoded) {
        return; // empty cell — not drawn / not lit
    }

    // Append the cell's linear index in the SAME convention the scatter + the
    // compute stages recover it (ij = cell % size.x, cell / size.x), and bump the
    // instance count. The append slot is this cell's instance id.
    const uint slot = atomicAdd(drawArgs[1], 1u);
    compactedCells[slot] = uint(cell.y * size.x + cell.x);
}

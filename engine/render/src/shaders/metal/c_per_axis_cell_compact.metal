#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

// Per-axis empty-cell compaction pre-pass (#1961). Mirror of
// shaders/c_per_axis_cell_compact.glsl. Scans one per-axis distance canvas and
// atomic-appends each occupied cell's linear index into a per-axis SSBO region,
// bumping the indirect instanced-draw instance count, so the scatter composite
// draws only occupied cells instead of the full worst-case grid. Dispatched
// once per axis (the caller binds this axis's region of both SSBOs at offset 0).
// Reads the distance via access::read — no image-atomic scratch (so this kernel
// is NOT in functionUsesImageAtomicScratch). Cardinal byte-identity is
// structural: per-axis canvases are only allocated at non-zero residual yaw.

// Per-axis canvas empty sentinel (#1458) — mirror of the GLSL.
constant int kEmptyDistanceEncoded = 0x7FFFFFFF;

// #2256: compute-indirect sub-record indices in the 256 B PerAxisCellIndirect
// block + the 1-D group size the per-axis compute stages iterate the list with
// (mirror of the GLSL / ir_render_types.hpp constants).
constant uint kComputeBaseIdx = 8u;
constant uint kComputeCompletedIdx = 12u; // kComputeBaseIdx + completedGroups
constant uint kComputeGroupSize = 64u;

kernel void c_per_axis_cell_compact(
    texture2d<int, access::read> perAxisDistances [[texture(0)]],
    device uint* compactedCells [[buffer(25)]],
    device atomic_uint* drawArgs [[buffer(26)]],
    uint3 globalId [[thread_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint localIndex [[thread_index_in_threadgroup]]
) {
    const int2 cell = int2(globalId.xy);
    const int2 size =
        int2(int(perAxisDistances.get_width()), int(perAxisDistances.get_height()));

    // Append occupied cells. NO early return — every invocation must reach the
    // finalize barrier below uniformly. drawArgs layout matches
    // PerAxisCellDrawCommand: [1] = instanceCount (= append slot / occupied cell).
    if (cell.x < size.x && cell.y < size.y) {
        const int rawDist = perAxisDistances.read(uint2(cell)).x;
        if (rawDist < kEmptyDistanceEncoded) {
            const uint slot = atomic_fetch_add_explicit(&drawArgs[1], 1u, memory_order_relaxed);
            compactedCells[slot] = uint(cell.y * size.x + cell.x);
        }
    }

    // Last-threadgroup finalize (#2256): derive the compute-indirect dispatch
    // grid from the final occupied count so the per-axis compute stages walk only
    // occupied cells. Mirrors c_voxel_visibility_compact.metal's completedGroups
    // pattern (atomic count read so the last group sees every append).
    threadgroup_barrier(mem_flags::mem_device);
    if (localIndex == 0u) {
        const uint finished =
            atomic_fetch_add_explicit(&drawArgs[kComputeCompletedIdx], 1u, memory_order_relaxed) + 1u;
        const uint totalGroups = groupCount.x * groupCount.y;
        if (finished == totalGroups) {
            const uint count = atomic_load_explicit(&drawArgs[1], memory_order_relaxed);
            atomic_store_explicit(
                &drawArgs[kComputeBaseIdx + 0u],
                max((count + kComputeGroupSize - 1u) / kComputeGroupSize, 1u),
                memory_order_relaxed
            );
            atomic_store_explicit(&drawArgs[kComputeBaseIdx + 1u], 1u, memory_order_relaxed);
            atomic_store_explicit(&drawArgs[kComputeBaseIdx + 2u], 1u, memory_order_relaxed);
            atomic_store_explicit(&drawArgs[kComputeBaseIdx + 3u], count, memory_order_relaxed);
        }
    }
}

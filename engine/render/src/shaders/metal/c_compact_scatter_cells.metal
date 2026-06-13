// Project: Irreden Engine
// File: metal/c_compact_scatter_cells.metal
// Per-axis scatter compaction (#1310 perf follow-up) — Metal mirror of
// c_compact_scatter_cells.glsl. Scans one per-axis canvas for non-empty
// cells and appends their linear indices for the indirect instanced
// scatter draw, so the draw launches one instance per NON-EMPTY cell
// instead of one per canvas cell.

#include <metal_stdlib>
using namespace metal;

// GL DrawElementsIndirectCommand and MTLDrawIndexedPrimitivesIndirectArguments
// share this 5-uint layout:
//   [0] indexCount   (pre-seeded by the CPU)
//   [1] instanceCount (authored here via atomic append)
//   [2] firstIndex   (pre-seeded 0)
//   [3] baseVertex   (pre-seeded 0)
//   [4] baseInstance (pre-seeded 0)
constant uint kSlotInstanceCount = 1;

kernel void c_compact_scatter_cells(
    texture2d<float, access::read> triangleColors [[texture(0)]],
    device uint* scatterCells [[buffer(25)]],
    device atomic_uint* drawArgs [[buffer(26)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const uint2 canvasSize = uint2(triangleColors.get_width(), triangleColors.get_height());
    const uint2 ij = globalId.xy;
    if (ij.x >= canvasSize.x || ij.y >= canvasSize.y) return;
    // Mirror the scatter vertex stage's empty test (kColorClear alpha == 0).
    const float4 color = triangleColors.read(ij);
    if (color.a < 0.1f) return;
    const uint slot = atomic_fetch_add_explicit(
        &drawArgs[kSlotInstanceCount],
        1u,
        memory_order_relaxed
    );
    scatterCells[slot] = ij.y * canvasSize.x + ij.x;
}

/*
 * Project: Irreden Engine
 * File: c_compact_scatter_cells.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: June 2026
 * -----
 * Per-axis scatter compaction (#1310 perf follow-up): scan one per-axis
 * canvas for non-empty cells and append their linear indices for the
 * indirect instanced scatter draw, so the draw launches one instance per
 * NON-EMPTY cell instead of one per canvas cell. The brute-force sweep
 * degenerated >90% of its instances on typical frames — the worst-case
 * (2W, W+H) canvas is mostly empty at every zoom.
 */

#version 450 core

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout (binding = 0, rgba8) uniform readonly image2D triangleColors;

layout(std430, binding = 25) writeonly buffer ScatterCells {
    uint scatterCells[];
};

// GL DrawElementsIndirectCommand and MTLDrawIndexedPrimitivesIndirectArguments
// share this 5-uint layout. indexCount / firstIndex / baseVertex /
// baseInstance are pre-seeded by the CPU each axis pass; only instanceCount
// is authored here.
layout(std430, binding = 26) buffer ScatterDrawArgs {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
};

void main() {
    const ivec2 canvasSize = imageSize(triangleColors);
    const ivec2 ij = ivec2(gl_GlobalInvocationID.xy);
    if (ij.x >= canvasSize.x || ij.y >= canvasSize.y) return;
    // Mirror the scatter vertex shader's empty test (kColorClear alpha == 0).
    const vec4 color = imageLoad(triangleColors, ij);
    if (color.a < 0.1) return;
    const uint slot = atomicAdd(instanceCount, 1u);
    scatterCells[slot] = uint(ij.y * canvasSize.x + ij.x);
}

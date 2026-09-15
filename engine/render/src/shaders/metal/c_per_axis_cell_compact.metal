#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

// Mirror of shaders/c_per_axis_cell_compact.glsl. Dispatched once per axis
// (the caller binds this axis's region of both SSBOs at offset 0). Reads the
// distance via access::read — no image-atomic scratch, so this kernel is NOT
// in functionUsesImageAtomicScratch. Cardinal byte-identity is structural:
// per-axis canvases are only allocated at non-zero residual yaw.

// Per-axis canvas empty sentinel — mirror of the GLSL.
constant int kEmptyDistanceEncoded = 0x7FFFFFFF;

// The compute-indirect dims at [8..11] are written by c_per_axis_cell_finalize,
// not here — deliberately: this kernel sweeps the FULL per-axis grid, so a
// cross-workgroup completion barrier here would stall every workgroup on the
// hot scan.
kernel void c_per_axis_cell_compact(
    texture2d<int, access::read> perAxisDistances [[texture(0)]],
    device uint* compactedCells [[buffer(25)]],
    device atomic_uint* drawArgs [[buffer(26)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 cell = int2(globalId.xy);
    const int2 size =
        int2(int(perAxisDistances.get_width()), int(perAxisDistances.get_height()));
    if (cell.x >= size.x || cell.y >= size.y) {
        return;
    }

    const int rawDist = perAxisDistances.read(uint2(cell)).x;
    if (rawDist >= kEmptyDistanceEncoded) {
        return; // empty cell — not drawn / not lit
    }

    // drawArgs layout matches PerAxisCellDrawCommand: [1] = instanceCount. The
    // append slot is this cell's instance id in the indirect draw.
    const uint slot = atomic_fetch_add_explicit(&drawArgs[1], 1u, memory_order_relaxed);
    compactedCells[slot] = uint(cell.y * size.x + cell.x);
}

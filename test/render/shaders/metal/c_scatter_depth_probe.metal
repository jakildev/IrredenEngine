#include "../../../../engine/render/src/shaders/metal/ir_scatter_depth.metal"
kernel void c_scatter_depth_probe(device float* depths [[buffer(0)]],
                                  uint3 gid [[thread_position_in_grid]]) {
    const uint bands[8] = {0u, 1u, 131071u, 262143u, 262144u, 262145u, 524286u, 524287u};
    const uint i = gid.x;
    const uint q = bands[i >> 5u] * 32u + (i & 31u);
    depths[i] = scatterFinalDepth(
        float(q >> 5u) * kScatterCellTieBand,
        float((q >> 1u) & 15u) * kScatterCellTieStep, (q & 1u) != 0u);
}

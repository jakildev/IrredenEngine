#include "../../../../engine/render/src/shaders/metal/ir_scatter_depth.metal"
kernel void c_scatter_depth_probe(device float* depths [[buffer(0)]],
                                  uint3 gid [[thread_position_in_grid]]) {
    const uint bands[8] = {0u, 1u, 131071u, 262143u, 262144u, 262145u, 524286u, 524287u};
    const uint i = gid.x;
    depths[i] = scatterFinalDepth(
        float(bands[i >> 4u]) * kScatterCellTieBand,
        float(i & 15u) * kScatterCellTieStep);
}

#version 450 core
#include "../../../engine/render/src/shaders/ir_scatter_depth.glsl"
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(std430, binding = 0) buffer Results { float depths[]; };
void main() {
    const uint bands[8] = uint[8](0u, 1u, 131071u, 262143u, 262144u, 262145u, 524286u, 524287u);
    uint i = gl_GlobalInvocationID.x;
    uint q = bands[i >> 5u] * 32u + (i & 31u);
    depths[i] = scatterFinalDepth(
        float(q >> 5u) * kScatterCellTieBand,
        float((q >> 1u) & 15u) * kScatterCellTieStep, (q & 1u) != 0u);
}

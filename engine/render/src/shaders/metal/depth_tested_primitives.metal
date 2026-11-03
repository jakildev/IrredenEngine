#include <metal_stdlib>
using namespace metal;

struct PrimitiveVertexIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};
struct PrimitiveVertexOut {
    float4 position [[position]];
    float4 color;
};

vertex PrimitiveVertexOut v_depth_tested_primitives(PrimitiveVertexIn in [[stage_in]]) {
    PrimitiveVertexOut out;
    // Framebuffer-target parity with the voxel composite; Metal depth is [0,1].
    out.position = float4(in.position.x, -in.position.y, in.position.z, 1.0);
    out.color = in.color;
    return out;
}

fragment float4 f_depth_tested_primitives(PrimitiveVertexOut in [[stage_in]]) {
    return in.color;
}

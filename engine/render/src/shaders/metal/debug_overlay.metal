#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

struct DebugOverlayData {
    float4x4 mvp;
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut v_debug_overlay(
    VertexIn in [[stage_in]],
    constant DebugOverlayData& ubo [[buffer(15)]]
) {
    VertexOut out;
    out.position = ubo.mvp * float4(in.position, 0.0, 1.0);
    out.position.y = -out.position.y;
    out.color = in.color;
    return out;
}

fragment float4 f_debug_overlay(VertexOut in [[stage_in]]) {
    return in.color;
}

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoords [[attribute(1)]];
};

struct FrameDataFramebuffer {
    float4x4 model;
    float2 textureOffset;
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoords;
};

vertex VertexOut v_framebuffer_to_screen(
    VertexIn in [[stage_in]],
    texture2d<float> screenTexture [[texture(0)]],
    constant FrameDataFramebuffer& frameData [[buffer(2)]]
) {
    VertexOut out;
    const float2 textureSize = float2(screenTexture.get_width(), screenTexture.get_height());
    out.position = frameData.model * float4(in.position, 1.0, 1.0);
    out.position.y = -out.position.y;
    out.texCoords = float2(in.texCoords.x, 1.0 - in.texCoords.y) +
                    (frameData.textureOffset / textureSize);
    return out;
}

fragment float4 f_framebuffer_to_screen(
    VertexOut in [[stage_in]],
    texture2d<float> screenTexture [[texture(0)]]
) {
    constexpr sampler screenSampler(coord::normalized, address::clamp_to_edge, filter::nearest);
    return screenTexture.sample(screenSampler, in.texCoords);
}

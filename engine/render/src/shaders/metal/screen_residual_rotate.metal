#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoords [[attribute(1)]];
};

struct FrameDataScreenResidualRotate {
    float4x4 mvpMatrix;
    float2 textureOffset;
    float residualYaw;
    float _pad0;
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoords;
};

constant float kIdentityYawEpsilon = 1e-6f;

vertex VertexOut v_screen_residual_rotate(
    VertexIn in [[stage_in]],
    texture2d<float> screenTexture [[texture(0)]],
    constant FrameDataScreenResidualRotate& frameData [[buffer(16)]]
) {
    VertexOut out;
    const float2 ts = float2(screenTexture.get_width(), screenTexture.get_height());
    out.position = frameData.mvpMatrix * float4(in.position, 1.0, 1.0);
    out.position.y = -out.position.y;
    out.texCoords = float2(in.texCoords.x, 1.0 - in.texCoords.y) +
                    (frameData.textureOffset / ts);
    return out;
}

fragment float4 f_screen_residual_rotate(
    VertexOut in [[stage_in]],
    texture2d<float> screenTexture [[texture(0)]],
    constant FrameDataScreenResidualRotate& frameData [[buffer(16)]]
) {
    constexpr sampler nearestSampler(coord::normalized, address::clamp_to_edge, filter::nearest);
    constexpr sampler linearSampler(coord::normalized, address::clamp_to_edge, filter::linear);

    if (abs(frameData.residualYaw) < kIdentityYawEpsilon) {
        return screenTexture.sample(nearestSampler, in.texCoords);
    }

    const float2 ts = float2(screenTexture.get_width(), screenTexture.get_height());
    const float2 pixelPos = in.texCoords * ts;
    const float2 center = ts * 0.5f;
    const float2 centered = pixelPos - center;

    const float c = cos(-frameData.residualYaw);
    const float s = sin(-frameData.residualYaw);
    const float2 rotated = float2(c * centered.x - s * centered.y,
                                  s * centered.x + c * centered.y);
    const float2 sampleUV = (rotated + center) / ts;

    return screenTexture.sample(linearSampler, sampleUV);
}

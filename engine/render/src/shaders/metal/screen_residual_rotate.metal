#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoords [[attribute(1)]];
};

// UBO layout preserved (matches GLSL + CPU FrameDataScreenResidualRotate)
// even though residualYaw is unused after T-293: residual yaw is folded
// into faceDeform[] in the trixel emit shaders, so this stage is a pure
// framebuffer passthrough now.
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
    texture2d<float> screenTexture [[texture(0)]]
) {
    constexpr sampler nearestSampler(coord::normalized, address::clamp_to_edge, filter::nearest);
    return screenTexture.sample(nearestSampler, in.texCoords);
}

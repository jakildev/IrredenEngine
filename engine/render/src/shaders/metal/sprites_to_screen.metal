#include <metal_stdlib>
using namespace metal;

// Vertex stream supplied by QuadVAOArrays — six (xy, uv) vertices forming a
// unit quad with positions in [-0.5, 0.5] and UVs in [0, 1].
struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoords [[attribute(1)]];
};

// Mirrors C++ FrameDataSpritesToScreen / GLSL FrameData. Bound to slot 0.
struct FrameDataSpritesToScreen {
    float4x4 projection;
};

// Mirrors C++ GpuSpriteInstance / GLSL SpriteInstance. Bound to slot 25 as
// a device buffer; one entry per drawn sprite, indexed by [[instance_id]].
struct SpriteInstance {
    float4 screenPosSize; // (screenX, screenY, sizeX, sizeY)
    float4 uvRect;        // (u0, v0, u1, v1)
    float4 tintRgba;
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoords;
    float4 tint;
};

vertex VertexOut v_sprites_to_screen(
    VertexIn in [[stage_in]],
    uint instanceId [[instance_id]],
    constant FrameDataSpritesToScreen &frame [[buffer(0)]],
    const device SpriteInstance *instances [[buffer(25)]]
) {
    SpriteInstance s = instances[instanceId];
    float2 quadFrac = in.position + float2(0.5);
    float2 worldXY  = s.screenPosSize.xy + quadFrac * s.screenPosSize.zw;

    VertexOut out;
    out.position = frame.projection * float4(worldXY, 0.0, 1.0);
    // Match framebuffer_to_screen.metal: Metal NDC Y points the opposite
    // way from GL once the viewport flip lands; the sign-flip keeps sprites
    // upright on both backends. The texCoord V is flipped for the same
    // reason.
    out.position.y = -out.position.y;
    out.texCoords  = mix(s.uvRect.xy, s.uvRect.zw, float2(in.texCoords.x, 1.0 - in.texCoords.y));
    out.tint       = s.tintRgba;
    return out;
}

fragment float4 f_sprites_to_screen(
    VertexOut in [[stage_in]],
    texture2d<float> spriteAtlas [[texture(0)]]
) {
    constexpr sampler atlasSampler(coord::normalized, address::clamp_to_edge, filter::nearest);
    float4 sampled = spriteAtlas.sample(atlasSampler, in.texCoords);
    return sampled * in.tint;
}

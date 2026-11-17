#include <metal_stdlib>
using namespace metal;

#include "ir_post_effects_common.metal"

// The vertex stage every FRAMEBUFFER_POST_EFFECTS pass shares, and the
// resolve: the picture (blurred when blur is on) plus the bloom glow, then
// the grade, drawn into the pass's output image texel for texel. Mirrors
// shaders/v_post_effects.glsl + f_post_effects.glsl.

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoords [[attribute(1)]];
};

// Covers the bound target with one quad. The mvp is `framebufferSelfMvp`,
// which fills a target of any size. The texel coordinates are derived here
// from the first input's size so the vertex stage reads the previous pass's
// result too: a pass whose vertex stage reads no texture can start before its
// input finishes, and its GPU timestamp then includes that wait.
vertex PostEffectsVertexOut v_post_effects(
    VertexIn in [[stage_in]],
    texture2d<float> firstInput [[texture(0)]],
    constant FrameDataPostEffects &frameData [[buffer(2)]]
) {
    PostEffectsVertexOut out;
    out.position = frameData.mvpMatrix * float4(in.position, 1.0, 1.0);
    out.position.y = -out.position.y;
    // Every target is its input's twin or a whole fraction of it, so the
    // texel read is the texel written: no origin flip here, unlike a stage
    // that samples for the screen.
    out.texelCoords = in.texCoords * float2(firstInput.get_width(), firstInput.get_height());
    return out;
}

// `glow` is bloom level 0 with every coarser level folded in; the picture
// itself when bloom is off, where it is never read.
fragment float4 f_post_effects(
    PostEffectsVertexOut in [[stage_in]],
    texture2d<float> picture [[texture(0)]],
    texture2d<float> glow [[texture(1)]],
    constant FrameDataPostEffects &frameData [[buffer(2)]]
) {
    const int2 pixel = int2(floor(in.texelCoords));
    float4 colour = postEffectsFetch(picture, pixel);
    if (frameData.switches.y != 0) {
        const float3 bloom = postEffectsUpsample(glow, pixel).rgb * frameData.bloomParams.z;
        if (frameData.switches.z == kPostEffectsBloomBlendScreen) {
            colour.rgb = float3(1.0f) -
                         (float3(1.0f) - colour.rgb) * (float3(1.0f) - clamp(bloom, 0.0f, 1.0f));
        } else {
            colour.rgb += bloom;
        }
    }
    if (frameData.switches.w != 0) {
        colour.rgb = postEffectsGrade(colour.rgb, frameData);
    }
    return float4(clamp(colour.rgb, float3(0.0f), float3(1.0f)), colour.a);
}

#include <metal_stdlib>
using namespace metal;

#include "ir_post_effects_common.metal"

// One bloom level from the level above it, at half its resolution. Mirrors
// shaders/f_post_effects_bloom_downsample.glsl.

fragment float4 f_post_effects_bloom_downsample(
    PostEffectsVertexOut in [[stage_in]],
    texture2d<float> finerLevel [[texture(0)]],
    constant FrameDataPostEffects &frameData [[buffer(2)]]
) {
    return postEffectsDownsample(finerLevel, int2(floor(in.texelCoords * 0.5f)), false, frameData);
}

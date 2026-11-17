#include <metal_stdlib>
using namespace metal;

#include "ir_post_effects_common.metal"

// Bloom level 0: the thresholded picture at half resolution. Mirrors
// shaders/f_post_effects_bloom_prefilter.glsl.

fragment float4 f_post_effects_bloom_prefilter(
    PostEffectsVertexOut in [[stage_in]],
    texture2d<float> picture [[texture(0)]],
    constant FrameDataPostEffects &frameData [[buffer(2)]]
) {
    return postEffectsDownsample(picture, int2(floor(in.texelCoords * 0.5f)), true, frameData);
}

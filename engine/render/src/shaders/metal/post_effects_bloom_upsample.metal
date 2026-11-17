#include <metal_stdlib>
using namespace metal;

#include "ir_post_effects_common.metal"

// Folds every coarser bloom level into this one: this level's own
// downsample mixed toward the upsampled level below it by the scatter.
// Mirrors shaders/f_post_effects_bloom_upsample.glsl.

fragment float4 f_post_effects_bloom_upsample(
    PostEffectsVertexOut in [[stage_in]],
    texture2d<float> thisLevel [[texture(0)]],
    texture2d<float> coarserLevel [[texture(1)]],
    constant FrameDataPostEffects &frameData [[buffer(2)]]
) {
    const int2 pixel = int2(floor(in.texelCoords));
    return mix(
        postEffectsFetch(thisLevel, pixel),
        postEffectsUpsample(coarserLevel, pixel),
        frameData.bloomParams.w
    );
}

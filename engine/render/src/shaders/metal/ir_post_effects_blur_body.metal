// One pass of the separable Gaussian blur along IR_POST_EFFECTS_BLUR_AXIS,
// which the including wrapper defines as int2(1, 0) or int2(0, 1) beside the
// fragment's name, IR_POST_EFFECTS_BLUR_FRAGMENT_NAME. Mirrors
// ../ir_post_effects_blur_body.glsl and `IRPrefab::PostEffects::blurAxis`.

#include <metal_stdlib>
using namespace metal;

#include "ir_post_effects_common.metal"

fragment float4 IR_POST_EFFECTS_BLUR_FRAGMENT_NAME(
    PostEffectsVertexOut in [[stage_in]],
    texture2d<float> picture [[texture(0)]],
    constant FrameDataPostEffects &frameData [[buffer(2)]]
) {
    const int2 pixel = int2(floor(in.texelCoords));
    float4 sum = postEffectsFetch(picture, pixel) * frameData.blurWeights[0].x;
    for (int offset = 1; offset <= kPostEffectsMaxBlurTaps; ++offset) {
        if (offset > frameData.switches.x) {
            break;
        }
        const int2 reach = IR_POST_EFFECTS_BLUR_AXIS * offset;
        sum += (postEffectsFetch(picture, pixel + reach) + postEffectsFetch(picture, pixel - reach)) *
               frameData.blurWeights[offset >> 2][offset & 3];
    }
    return sum;
}

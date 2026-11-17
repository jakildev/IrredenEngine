// One pass of the separable Gaussian blur along IR_POST_EFFECTS_BLUR_AXIS,
// which the including wrapper defines as ivec2(1, 0) or ivec2(0, 1). Mirrors
// metal/ir_post_effects_blur_body.metal and `IRPrefab::PostEffects::blurAxis`.

in vec2 TexelCoords;

out vec4 FragColor;

layout (binding = 0) uniform sampler2D picture;

void main() {
    ivec2 pixel = ivec2(floor(TexelCoords));
    vec4 sum = postEffectsFetch(picture, pixel) * blurWeights[0].x;
    for (int offset = 1; offset <= kPostEffectsMaxBlurTaps; ++offset) {
        if (offset > switches.x) {
            break;
        }
        ivec2 reach = IR_POST_EFFECTS_BLUR_AXIS * offset;
        sum += (postEffectsFetch(picture, pixel + reach) + postEffectsFetch(picture, pixel - reach)) *
               blurWeights[offset >> 2][offset & 3];
    }
    FragColor = sum;
}

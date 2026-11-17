#version 450 core

#include "ir_post_effects_common.glsl"

// The resolve: the picture (blurred when blur is on) plus the bloom glow,
// then the grade, drawn into the pass's output image texel for texel.
// Mirrors metal/post_effects.metal.

in vec2 TexelCoords;

out vec4 FragColor;

layout (binding = 0) uniform sampler2D picture;
// Bloom level 0 with every coarser level folded in; the picture itself when
// bloom is off, where it is never read.
layout (binding = 1) uniform sampler2D glow;

void main() {
    ivec2 pixel = ivec2(floor(TexelCoords));
    vec4 colour = postEffectsFetch(picture, pixel);
    if (switches.y != 0) {
        vec3 bloom = postEffectsUpsample(glow, pixel).rgb * bloomParams.z;
        if (switches.z == kPostEffectsBloomBlendScreen) {
            colour.rgb = vec3(1.0) - (vec3(1.0) - colour.rgb) * (vec3(1.0) - clamp(bloom, 0.0, 1.0));
        } else {
            colour.rgb += bloom;
        }
    }
    if (switches.w != 0) {
        colour.rgb = postEffectsGrade(colour.rgb);
    }
    FragColor = vec4(clamp(colour.rgb, 0.0, 1.0), colour.a);
}

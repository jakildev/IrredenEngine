#version 450 core

#include "ir_post_effects_common.glsl"

// Folds every coarser bloom level into this one: this level's own
// downsample mixed toward the upsampled level below it by the scatter.
// Mirrors metal/post_effects_bloom_upsample.metal.

in vec2 TexelCoords;

out vec4 FragColor;

layout (binding = 0) uniform sampler2D thisLevel;
layout (binding = 1) uniform sampler2D coarserLevel;

void main() {
    ivec2 pixel = ivec2(floor(TexelCoords));
    FragColor = mix(
        postEffectsFetch(thisLevel, pixel),
        postEffectsUpsample(coarserLevel, pixel),
        bloomParams.w
    );
}

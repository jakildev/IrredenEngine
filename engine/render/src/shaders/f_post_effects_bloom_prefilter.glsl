#version 450 core

#include "ir_post_effects_common.glsl"

// Bloom level 0: the thresholded picture at half resolution. Mirrors
// metal/post_effects_bloom_prefilter.metal.

in vec2 TexelCoords;

out vec4 FragColor;

layout (binding = 0) uniform sampler2D picture;

void main() {
    FragColor = postEffectsDownsample(picture, ivec2(floor(TexelCoords * 0.5)), true);
}

#version 450 core

#include "ir_post_effects_common.glsl"

// One bloom level from the level above it, at half its resolution. Mirrors
// metal/post_effects_bloom_downsample.metal.

in vec2 TexelCoords;

out vec4 FragColor;

layout (binding = 0) uniform sampler2D finerLevel;

void main() {
    FragColor = postEffectsDownsample(finerLevel, ivec2(floor(TexelCoords * 0.5)), false);
}

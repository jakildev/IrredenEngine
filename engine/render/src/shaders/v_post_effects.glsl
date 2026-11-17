#version 450 core

#include "ir_post_effects_common.glsl"

// Covers the bound target with one quad for every FRAMEBUFFER_POST_EFFECTS
// pass. The mvp is `framebufferSelfMvp`, which fills a target of any size.

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

// Texel coordinates of the pass's first input. Derived here from that
// image's size so the vertex stage reads the previous pass's result too: a
// pass whose vertex stage reads no texture can start before its input
// finishes, and its GPU timestamp then includes that wait.
out vec2 TexelCoords;

layout (binding = 0) uniform sampler2D firstInput;

void main() {
    TexelCoords = aTexCoords * vec2(textureSize(firstInput, 0));
    gl_Position = mvpMatrix * vec4(aPos, 1.0, 1.0);
}

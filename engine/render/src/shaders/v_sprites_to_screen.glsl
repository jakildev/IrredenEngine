/*
 * Project: Irreden Engine
 * File: v_sprites_to_screen.glsl
 *
 * Vertex shader for the SPRITE_TO_SCREEN pass. Each instance draws one
 * textured quad at the screen-pixel position written by the CPU-side
 * gather-and-sort step. The QuadVAOArrays VAO supplies aPos in
 * [-0.5, 0.5] and aTexCoords in [0, 1]; gl_InstanceID indexes into the
 * SpriteInstancesBuffer SSBO for per-sprite transform.
 */

#version 450 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 vTexCoords;
out vec4 vTint;

layout (std140, binding = 0) uniform FrameData {
    mat4 projection;
};

struct SpriteInstance {
    vec4 screenPosSize; // (screenX, screenY, sizeX, sizeY) in screen pixels
    vec4 uvRect;        // (u0, v0, u1, v1)
    vec4 tintRgba;
};

layout (std430, binding = 25) readonly buffer SpriteInstancesBuffer {
    SpriteInstance instances[];
};

void main() {
    SpriteInstance s = instances[gl_InstanceID];
    vec2 quadFrac = aPos + vec2(0.5);            // [0, 1] from top-left
    vec2 worldXY  = s.screenPosSize.xy + quadFrac * s.screenPosSize.zw;
    gl_Position   = projection * vec4(worldXY, 0.0, 1.0);
    vTexCoords    = mix(s.uvRect.xy, s.uvRect.zw, aTexCoords);
    vTint         = s.tintRgba;
}

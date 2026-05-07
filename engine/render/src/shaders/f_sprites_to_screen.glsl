/*
 * Project: Irreden Engine
 * File: f_sprites_to_screen.glsl
 *
 * Fragment shader for the SPRITE_TO_SCREEN pass. Samples the bound atlas
 * at the per-instance UV rect interpolated in the vertex stage and
 * multiplies by the per-instance tint. Alpha blending is enabled via
 * pipeline state by System<SPRITE_TO_SCREEN>; no per-sprite blend mode.
 */

#version 460 core

in vec2 vTexCoords;
in vec4 vTint;

out vec4 FragColor;

layout (binding = 0) uniform sampler2D spriteAtlas;

void main() {
    vec4 sampled = texture(spriteAtlas, vTexCoords);
    FragColor = sampled * vTint;
}

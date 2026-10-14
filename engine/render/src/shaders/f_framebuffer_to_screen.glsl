/*
 * Project: Irreden Engine
 * File: f_framebuffer_to_screen.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 450 core
out vec4 FragColor;

in vec2 TexCoords;

layout (binding = 0) uniform sampler2D screenTexture;

// Copies colour only, like the Metal twin. Carrying the framebuffer's depth
// into gl_FragDepth would fail the screen's depth test on every pixel left at
// the cleared far depth, dropping overlays composited without a depth write
// (secondary viewports, debug lines over empty background).
void main()
{
    FragColor = texture(screenTexture, TexCoords);
}
/*
 * Project: Irreden Engine
 * File: f_framebuffer_to_screen.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 460 core
out vec4 FragColor;

in vec2 TexCoords;

layout (binding = 0) uniform sampler2D screenTexture;
layout (binding = 1) uniform sampler2D depthTexture;

void main()
{
    FragColor = texture(screenTexture, TexCoords);
    gl_FragDepth = texture(depthTexture, TexCoords).r;

}
/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\shaders\v_framebuffer_screen.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 460 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

layout (binding = 0) uniform sampler2D screenTexture;
layout (binding = 1) uniform sampler2D depthTexture;

layout (std140, binding = 2) uniform FrameData {
    mat4 model;
    vec2 textureOffset;
};

void main() {
    ivec2 textureSize = textureSize(screenTexture, 0);
    TexCoords = aTexCoords + (textureOffset / vec2(textureSize));
    gl_Position = model * vec4(aPos, 1.0f, 1.0f);
}

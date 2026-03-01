/*
 * Project: Irreden Engine
 * File: v_trixel_to_framebuffer.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 460 core
layout (location = 0) in vec2 aPos;

out vec2 TexCoords;

layout (binding = 0) uniform sampler2D triangleColors;
layout (binding = 1) uniform isampler2D  triangleDistances;

layout (std140, binding = 3) uniform FrameDataIsoTriangles {
    mat4 mpMatrix;
    vec2 zoomLevel;
    vec2 canvasOffset;
    vec2 textureOffset;
    vec2 mouseHoveredTriangleIndex;
    vec2 _padding0;
};

void main() {
    ivec2 textureSize = textureSize(triangleColors, 0);
    TexCoords = vec2(aPos.x, -aPos.y) + 0.50 + (textureOffset / vec2(textureSize));
    gl_Position = mpMatrix * vec4(aPos, 1.0f, 1.0f);
}

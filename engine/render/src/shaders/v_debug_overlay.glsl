#version 460 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;

out vec4 vColor;

layout (std140, binding = 15) uniform DebugOverlayData {
    mat4 mvp;
};

void main() {
    vColor = aColor;
    gl_Position = mvp * vec4(aPos, 0.0, 1.0);
}

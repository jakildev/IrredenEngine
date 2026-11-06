#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;

void main() {
    gl_Position = vec4(aPosition.xy, aPosition.z * 2.0 - 1.0, 1.0);
    vColor = aColor;
}

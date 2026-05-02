#version 460 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

layout (binding = 0) uniform sampler2D screenTexture;

layout (std140, binding = 16) uniform FrameData {
    mat4 mvpMatrix;
    vec2 textureOffset;
    float residualYaw;
    float _pad0;
};

void main() {
    ivec2 ts = textureSize(screenTexture, 0);
    TexCoords = aTexCoords + (textureOffset / vec2(ts));
    gl_Position = mvpMatrix * vec4(aPos, 1.0f, 1.0f);
}

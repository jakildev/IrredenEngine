#version 450 core
out vec4 FragColor;

in vec2 TexCoords;

layout (binding = 0) uniform sampler2D screenTexture;
layout (binding = 1) uniform sampler2D depthTexture;

// SCREEN_SPACE_RESIDUAL_ROTATE became a pure framebuffer passthrough after
// T-293 — residual yaw is now folded into the trixel emit shaders via
// `faceDeform[face]`, so there is no leftover screen-space rotation for
// this stage to apply. UBO layout is preserved (matches Metal + the CPU
// FrameDataScreenResidualRotate mirror) so existing creations don't need
// to re-register the stage; the unused residualYaw_ field stays at 0.
layout (std140, binding = 16) uniform FrameData {
    mat4 mvpMatrix;
    vec2 textureOffset;
    float residualYaw;
    float _pad0;
};

void main() {
    FragColor = texture(screenTexture, TexCoords);
    gl_FragDepth = texture(depthTexture, TexCoords).r;
}

#version 450 core
layout (location = 0) in vec2 aPos;

out vec2 CanvasPositionFromCenter;

layout (binding = 0) uniform sampler2D canvasColors;

// Declared identically in f_gui_canvas_to_screen.glsl: a uniform block shared
// by name across stages must be member-identical or the program fails to link
// on strict drivers.
layout (std140, binding = 2) uniform FrameDataGuiCanvasToScreen {
    mat4 mpMatrix;
    vec2 texelScreenPx;
    int sampleLayout;
    int _pad;
};

void main() {
    ivec2 canvasSize = textureSize(canvasColors, 0);
    // Centered texel units, V flipped so canvas row 0 lands at the top of the
    // screen; the fragment adds half the canvas size back.
    CanvasPositionFromCenter = vec2(aPos.x, -aPos.y) * vec2(canvasSize);
    gl_Position = mpMatrix * vec4(aPos, 1.0f, 1.0f);
}

#version 450 core

#include "ir_canvas_image_command.glsl"

// Places the unit quad over one image draw's clipped box of main framebuffer
// pixels. The box counts rows down the screen; the framebuffer's own rows run
// up it.

layout (location = 0) in vec2 aPos;

layout (std140, binding = 2) uniform FrameDataImageToFramebuffer {
    ivec2 targetSize;
    ivec2 padding;
    CanvasImageCommand command;
};

void main() {
    vec2 corner = mix(vec2(command.clipLo), vec2(command.clipHi), aPos + 0.5);
    vec2 clip = corner / vec2(targetSize) * 2.0 - 1.0;
    gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);
}

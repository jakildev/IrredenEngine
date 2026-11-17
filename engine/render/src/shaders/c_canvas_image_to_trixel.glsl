#version 450 core

// One image draw onto a trixel canvas: one thread per canvas texel of the
// draw's clipped box, dispatched from `clipLo`.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba8, binding = 0) writeonly uniform image2D canvasColors;
layout(r32i, binding = 1) uniform iimage2D canvasDistances;
layout(rgba8, binding = 2) readonly uniform image2D imageAtlas;

vec4 imageAtlasTexel(ivec2 texel) {
    return imageLoad(imageAtlas, texel);
}

#include "ir_canvas_image_sample.glsl"

layout(std430, binding = 12) buffer CommandBuffer {
    CanvasImageCommand command;
};

void main() {
    CanvasImageCommand cmd = command;
    ivec2 pixel = cmd.clipLo + ivec2(gl_GlobalInvocationID.xy);
    ivec2 canvasSize = imageSize(canvasColors);
    if (pixel.x >= cmd.clipHi.x || pixel.y >= cmd.clipHi.y ||
        !isInsideCanvas(pixel, canvasSize)) {
        return;
    }

    vec3 rgb;
    if (!canvasImageColor(cmd, pixel, rgb)) {
        return;
    }

    imageStore(canvasColors, pixel, vec4(rgb, 1.0));
    imageStore(canvasDistances, pixel, ivec4(cmd.distance));
}

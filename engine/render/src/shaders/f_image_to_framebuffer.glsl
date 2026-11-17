#version 450 core

// One image draw into the main framebuffer: one fragment per framebuffer pixel
// of the draw's clipped box, filtered as c_canvas_image_to_trixel filters a
// canvas texel, at the depth the composite gives a canvas texel of the draw's
// distance.

layout (binding = 2) uniform sampler2D imageAtlas;

vec4 imageAtlasTexel(ivec2 texel) {
    return texelFetch(imageAtlas, texel, 0);
}

#include "ir_canvas_image_sample.glsl"

layout (std140, binding = 1) uniform GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

layout (std140, binding = 2) uniform FrameDataImageToFramebuffer {
    ivec2 targetSize;
    ivec2 padding;
    CanvasImageCommand command;
};

out vec4 FragColor;

// The depth ir_trixel_to_framebuffer_body writes for a world-tier canvas texel
// at `dist` (clamped out of the foreground band, then normalizeDistance), plus
// half an encode step. The half step puts the image strictly behind a canvas
// texel of its own distance and strictly in front of the next one, so the
// order never rests on two programs rounding a depth alike.
float imageDepth(int dist) {
    int foregroundCeil = kMinTriangleDistance + kDepthForegroundBandWidth;
    int enc = max(dist, foregroundCeil + 1);
    return min(
        (float(enc - kMinTriangleDistance) + 0.5) /
            float(kMaxTriangleDistance - kMinTriangleDistance),
        1.0
    );
}

void main() {
    ivec2 pixel = ivec2(int(gl_FragCoord.x), targetSize.y - 1 - int(gl_FragCoord.y));
    vec3 rgb;
    if (!canvasImageColor(command, pixel, rgb)) {
        discard;
    }
    FragColor = vec4(rgb, 1.0);
    gl_FragDepth = imageDepth(command.distance);
}

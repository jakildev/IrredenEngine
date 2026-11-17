#include "ir_canvas_image_sample.metal"

// One image draw into the main framebuffer. Mirrors
// shaders/v_image_to_framebuffer.glsl + f_image_to_framebuffer.glsl.

struct ImageVertexIn {
    float2 position [[attribute(0)]];
};

struct GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

struct FrameDataImageToFramebuffer {
    int2 targetSize;
    int2 padding;
    CanvasImageCommand command;
};

struct ImageVertexOut {
    float4 position [[position]];
};

struct ImageFragmentOut {
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

// Places the unit quad over the draw's clipped box of main framebuffer pixels.
// The box counts rows down the screen; the framebuffer's own rows run up it.
vertex ImageVertexOut v_image_to_framebuffer(
    ImageVertexIn in [[stage_in]],
    constant FrameDataImageToFramebuffer &frameData [[buffer(2)]]
) {
    const float2 corner = mix(
        float2(frameData.command.clipLo),
        float2(frameData.command.clipHi),
        in.position + 0.5
    );
    const float2 clip = corner / float2(frameData.targetSize) * 2.0 - 1.0;
    ImageVertexOut out;
    out.position = float4(clip.x, -clip.y, 0.0, 1.0);
    out.position.y = -out.position.y;
    return out;
}

// The depth ir_trixel_to_framebuffer_body writes for a world-tier canvas texel
// at `dist` (clamped out of the foreground band, then normalizeDistance), plus
// half an encode step. The half step puts the image strictly behind a canvas
// texel of its own distance and strictly in front of the next one, so the
// order never rests on two programs rounding a depth alike.
inline float imageDepth(int dist, constant GlobalConstants &globals) {
    const int foregroundCeil = globals.kMinTriangleDistance + kDepthForegroundBandWidth;
    const int enc = max(dist, foregroundCeil + 1);
    return min(
        (float(enc - globals.kMinTriangleDistance) + 0.5) /
            float(globals.kMaxTriangleDistance - globals.kMinTriangleDistance),
        1.0
    );
}

// One fragment per framebuffer pixel of the draw's clipped box, filtered as
// c_canvas_image_to_trixel filters a canvas texel.
fragment ImageFragmentOut f_image_to_framebuffer(
    ImageVertexOut in [[stage_in]],
    constant GlobalConstants &globals [[buffer(1)]],
    constant FrameDataImageToFramebuffer &frameData [[buffer(2)]],
    texture2d<float, access::read> imageAtlas [[texture(2)]]
) {
    const int2 pixel = int2(
        int(in.position.x),
        frameData.targetSize.y - 1 - int(in.position.y)
    );
    float3 rgb;
    if (!canvasImageColor(imageAtlas, frameData.command, pixel, rgb)) {
        discard_fragment();
    }
    ImageFragmentOut out;
    out.color = float4(rgb, 1.0);
    out.depth = imageDepth(frameData.command.distance, globals);
    return out;
}

#include <metal_stdlib>
using namespace metal;

#include "ir_iso_common.metal"

// Screen-resolution composite of the GUI canvas; mirrors
// shaders/v_gui_canvas_to_screen.glsl + f_gui_canvas_to_screen.glsl. Each texel
// displays as the lattice triangle localTrixelFramebufferSamplePosition
// selects. Minimum-footprint samples use hard ownership; larger footprints
// anti-alias analytically against the texel across each edge.

struct VertexIn {
    float2 position [[attribute(0)]];
};

struct FrameDataGuiCanvasToScreen {
    float4x4 mpMatrix;
    // Screen pixels covered by one canvas texel on each axis.
    float2 texelScreenPx;
    // TrixelSampleLayout of the canvas: 1 displays lattice triangles, anything
    // else the rectangular texels of a full-resolution GUI canvas.
    int sampleLayout;
    int _pad;
};

struct VertexOut {
    float4 position [[position]];
    float2 canvasPositionFromCenter;
};

vertex VertexOut v_gui_canvas_to_screen(
    VertexIn in [[stage_in]],
    texture2d<float> canvasColors [[texture(0)]],
    constant FrameDataGuiCanvasToScreen& frameData [[buffer(2)]]
) {
    VertexOut out;
    const float2 canvasSize = float2(canvasColors.get_width(), canvasColors.get_height());
    out.position = frameData.mpMatrix * float4(in.position, 1.0, 1.0);
    out.position.y = -out.position.y;
    // The clip negation above already puts canvas row 0 at the top of the
    // screen target, so unlike the framebuffer gather the canvas V is not
    // flipped here.
    out.canvasPositionFromCenter = float2(in.position.x, in.position.y) * canvasSize;
    return out;
}

// Premultiplied color of texel `t`; transparent outside the canvas.
static float4 texelPremultiplied(texture2d<float> canvasColors, int2 t, int2 canvasSize) {
    if (any(t < int2(0)) || any(t >= canvasSize)) {
        return float4(0.0f);
    }
    const float4 c = canvasColors.read(uint2(t));
    return float4(c.rgb * c.a, c.a);
}

static float trixelEdgeCoverage(float nearest, float2 texelScreenPx) {
    const float effectiveDisplayScale = min(0.5f * texelScreenPx.x, texelScreenPx.y);
    if (effectiveDisplayScale <= 1.0f) {
        return 1.0f;
    }
    return clamp(nearest + 0.5f, 0.0f, 1.0f);
}

fragment float4 f_gui_canvas_to_screen(
    VertexOut in [[stage_in]],
    texture2d<float> canvasColors [[texture(0)]],
    constant FrameDataGuiCanvasToScreen& frameData [[buffer(2)]]
) {
    const int2 canvasSize = int2(canvasColors.get_width(), canvasColors.get_height());
    const float2 p = in.canvasPositionFromCenter + 0.5f * float2(canvasSize);
    float4 color;
    if (frameData.sampleLayout != 1) {
        color = texelPremultiplied(canvasColors, int2(floor(p)), canvasSize);
    } else {
        const int2 z1 = trixelOriginOffsetZ1(canvasSize);
        const int parity = localTrixelOriginParity(z1);
        const int2 t = int2(floor(localTrixelFramebufferSamplePosition(p, z1)));
        // Texel (X, Y) with even (X + Y + parity) displays as the LEFT-pointing
        // triangle (X+1, Y-1), (X+1, Y+1), (X, Y); odd as the RIGHT-pointing
        // triangle (X, Y-1), (X, Y+1), (X+1, Y). Each edge is shared with the
        // horizontal neighbor (vertical edge) or the texel above / below (the
        // two diagonals). Distances are signed, positive inside, in screen px.
        const bool left = ((t.x + t.y + parity) & 1) == 0;
        const float sx = frameData.texelScreenPx.x;
        const float sy = frameData.texelScreenPx.y;
        const float diagonalPx = rsqrt(1.0f / (sx * sx) + 1.0f / (sy * sy));
        float distVertical, distUpper, distLower;
        int2 acrossVertical;
        if (left) {
            distVertical = (float(t.x + 1) - p.x) * sx;
            distUpper = ((p.x + p.y) - float(t.x + t.y)) * diagonalPx;
            distLower = ((p.x - p.y) - float(t.x - t.y)) * diagonalPx;
            acrossVertical = int2(t.x + 1, t.y);
        } else {
            distVertical = (p.x - float(t.x)) * sx;
            distUpper = (float(t.x - t.y + 1) - (p.x - p.y)) * diagonalPx;
            distLower = (float(t.x + t.y + 1) - (p.x + p.y)) * diagonalPx;
            acrossVertical = int2(t.x - 1, t.y);
        }
        float nearest = distVertical;
        int2 across = acrossVertical;
        if (distUpper < nearest) {
            nearest = distUpper;
            across = int2(t.x, t.y - 1);
        }
        if (distLower < nearest) {
            nearest = distLower;
            across = int2(t.x, t.y + 1);
        }
        // A minimum-footprint triangle owns only two pixel samples. Fractional
        // coverage there leaves neither the triangle nor its neighbour solid.
        const float coverage = trixelEdgeCoverage(nearest, frameData.texelScreenPx);
        color = mix(
            texelPremultiplied(canvasColors, across, canvasSize),
            texelPremultiplied(canvasColors, t, canvasSize),
            coverage
        );
    }
    if (color.a < 1.0f / 255.0f) {
        discard_fragment();
    }
    // Straight alpha out: the screen blend is (srcAlpha, 1 - srcAlpha).
    return float4(color.rgb / color.a, color.a);
}

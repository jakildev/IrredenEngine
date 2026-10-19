#version 450 core

#include "ir_iso_common.glsl"

// Screen-resolution composite of the GUI canvas. Each texel displays as the
// lattice triangle localTrixelFramebufferSamplePosition selects. Minimum-
// footprint samples use hard ownership; larger footprints anti-alias against
// the texel across each edge instead of inheriting the game-resolution grid.

in vec2 CanvasPositionFromCenter;

layout (binding = 0) uniform sampler2D canvasColors;

layout (std140, binding = 2) uniform FrameDataGuiCanvasToScreen {
    mat4 mpMatrix;
    // Screen pixels covered by one canvas texel on each axis.
    vec2 texelScreenPx;
    // TrixelSampleLayout of the canvas: 1 displays lattice triangles, anything
    // else the rectangular texels of a full-resolution GUI canvas.
    int sampleLayout;
    int _pad;
};

out vec4 FragColor;

// Premultiplied color of texel `t`; transparent outside the canvas.
vec4 texelPremultiplied(ivec2 t, ivec2 canvasSize) {
    if (any(lessThan(t, ivec2(0))) || any(greaterThanEqual(t, canvasSize))) {
        return vec4(0.0);
    }
    vec4 c = texelFetch(canvasColors, t, 0);
    return vec4(c.rgb * c.a, c.a);
}

float trixelEdgeCoverage(float nearest, vec2 texelScreenPx) {
    float effectiveDisplayScale = min(0.5 * texelScreenPx.x, texelScreenPx.y);
    if (effectiveDisplayScale <= 1.0) {
        return 1.0;
    }
    return clamp(nearest + 0.5, 0.0, 1.0);
}

void main() {
    ivec2 canvasSize = textureSize(canvasColors, 0);
    vec2 p = CanvasPositionFromCenter + 0.5 * vec2(canvasSize);
    vec4 color;
    if (sampleLayout != 1) {
        color = texelPremultiplied(ivec2(floor(p)), canvasSize);
    } else {
        ivec2 z1 = trixelOriginOffsetZ1(canvasSize);
        int parity = localTrixelOriginParity(z1);
        ivec2 t = ivec2(floor(localTrixelFramebufferSamplePosition(p, z1)));
        // Texel (X, Y) with even (X + Y + parity) displays as the LEFT-pointing
        // triangle (X+1, Y-1), (X+1, Y+1), (X, Y); odd as the RIGHT-pointing
        // triangle (X, Y-1), (X, Y+1), (X+1, Y). Each edge is shared with the
        // horizontal neighbor (vertical edge) or the texel above / below (the
        // two diagonals). Distances are signed, positive inside, in screen px.
        bool left = ((t.x + t.y + parity) & 1) == 0;
        float sx = texelScreenPx.x;
        float sy = texelScreenPx.y;
        float diagonalPx = inversesqrt(1.0 / (sx * sx) + 1.0 / (sy * sy));
        float distVertical, distUpper, distLower;
        ivec2 acrossVertical;
        if (left) {
            distVertical = (float(t.x + 1) - p.x) * sx;
            distUpper = ((p.x + p.y) - float(t.x + t.y)) * diagonalPx;
            distLower = ((p.x - p.y) - float(t.x - t.y)) * diagonalPx;
            acrossVertical = ivec2(t.x + 1, t.y);
        } else {
            distVertical = (p.x - float(t.x)) * sx;
            distUpper = (float(t.x - t.y + 1) - (p.x - p.y)) * diagonalPx;
            distLower = (float(t.x + t.y + 1) - (p.x + p.y)) * diagonalPx;
            acrossVertical = ivec2(t.x - 1, t.y);
        }
        float nearest = distVertical;
        ivec2 across = acrossVertical;
        if (distUpper < nearest) {
            nearest = distUpper;
            across = ivec2(t.x, t.y - 1);
        }
        if (distLower < nearest) {
            nearest = distLower;
            across = ivec2(t.x, t.y + 1);
        }
        // A minimum-footprint triangle owns only two pixel samples. Fractional
        // coverage there leaves neither the triangle nor its neighbour solid.
        float coverage = trixelEdgeCoverage(nearest, texelScreenPx);
        color = mix(texelPremultiplied(across, canvasSize), texelPremultiplied(t, canvasSize), coverage);
    }
    if (color.a < 1.0 / 255.0) {
        discard;
    }
    // Straight alpha out: the screen blend is (srcAlpha, 1 - srcAlpha).
    FragColor = vec4(color.rgb / color.a, color.a);
}

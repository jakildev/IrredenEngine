#version 450 core

layout(local_size_x = 6, local_size_y = 11, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Font bitmap data, one packed row per uint: a glyph's rows start at its
// command's firstFontRow. Bit (cellWidth - 1 - col) is column col.
layout(std430, binding = 11) buffer FontBuffer {
    uint fontRows[];
};

struct GlyphDrawCommand {
    uint positionPacked;  // x | y<<16
    uint firstFontRow;    // the glyph's first row in fontRows
    uint colorPacked;     // RGBA as uint
    uint distance;
    uint styleFlags;      // low byte = fontSize; bit 8 = lattice-scaled glyphs;
                          // bits 16-19 / 20-23 = glyph cell width / height
};

layout(std430, binding = 12) buffer CommandBuffer {
    GlyphDrawCommand commands[];
};

layout(rgba8, binding = 0) writeonly uniform image2D canvasColors;
layout(r32i, binding = 1) uniform iimage2D canvasDistances;

void main() {
    GlyphDrawCommand cmd = commands[gl_WorkGroupID.x];
    ivec2 basePos = ivec2(int(cmd.positionPacked & 0xFFFFu), int(cmd.positionPacked >> 16u));
    uint fontSize = max(cmd.styleFlags & 0xFFu, 1u);
    uint cellWidth = (cmd.styleFlags >> 16u) & 0xFu;
    uint cellHeight = (cmd.styleFlags >> 20u) & 0xFu;

    uint col = gl_LocalInvocationID.x;
    uint row = gl_LocalInvocationID.y;
    if (col >= cellWidth || row >= cellHeight) {
        return;
    }

    uint rowBits = fontRows[cmd.firstFontRow + row];
    if (((rowBits >> (cellWidth - 1u - col)) & 1u) == 0u) {
        return;
    }

    ivec2 canvasSize = imageSize(canvasColors);
    vec4 color = unpackColor(cmd.colorPacked);
    int scale = int(fontSize);

    if ((cmd.styleFlags & 0x100u) == 0u) {
        ivec2 blockOrigin = basePos + ivec2(int(col) * scale, int(row) * scale);
        for (int dy = 0; dy < scale; dy++) {
            for (int dx = 0; dx < scale; dx++) {
                ivec2 pixel = blockOrigin + ivec2(dx, dy);
                if (!isInsideCanvas(pixel, canvasSize)) continue;
                imageStore(canvasColors, pixel, color);
                imageStore(canvasDistances, pixel, ivec4(int(cmd.distance)));
            }
        }
        return;
    }

    // Lattice display: glyph texel (col, row) is a left-pointing triangle when
    // col + row is even (the text origin is parity-aligned), right-pointing
    // when odd. Scaling that triangle by `scale` about the text origin gives a
    // lattice triangle of side `scale`, tiled by scale² unit triangles: one
    // texel in the apex column, three in the next, 2k + 1 in the k-th column
    // out from the apex, all centered on the source row.
    bool left = ((col + row) & 1u) == 0u;
    ivec2 center = basePos + ivec2(int(col), int(row)) * scale;
    for (int k = 0; k < scale; k++) {
        int x = left ? (center.x + k) : (center.x + scale - 1 - k);
        for (int dy = -k; dy <= k; dy++) {
            ivec2 pixel = ivec2(x, center.y + dy);
            if (!isInsideCanvas(pixel, canvasSize)) continue;
            imageStore(canvasColors, pixel, color);
            imageStore(canvasDistances, pixel, ivec4(int(cmd.distance)));
        }
    }
}

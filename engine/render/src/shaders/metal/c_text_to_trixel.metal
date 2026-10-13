#include "ir_iso_common.metal"

// Per-glyph text rasterization onto the trixel canvas.  Mirrors
// shaders/c_text_to_trixel.glsl — supports a fontSize multiplier so each
// glyph cell is rendered as fontSize x fontSize trixel pixels.
//
// Workgroup dimensions: (6, 11, 1) — one thread per (col, row) of the 6x11
// glyph bitmap.

struct GlyphDrawCommand {
    uint positionPacked;  // x | (y << 16)
    uint glyphIndex;      // 0-127 ASCII index into the font atlas
    uint colorPacked;     // RGBA8 packed
    uint distance;        // depth value to stamp
    uint styleFlags;      // low byte = fontSize; bit 8 = lattice-scaled glyphs
};

kernel void c_text_to_trixel(
    device const uint* fontRows [[buffer(11)]],
    device const GlyphDrawCommand* commands [[buffer(12)]],
    texture2d<float, access::write> canvasColors [[texture(0)]],
    texture2d<int, access::write> canvasDistances [[texture(1)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    const GlyphDrawCommand cmd = commands[groupId.x];
    const int2 basePos = int2(
        int(cmd.positionPacked & 0xFFFFu),
        int(cmd.positionPacked >> 16u)
    );
    const uint glyphIdx = cmd.glyphIndex;
    const uint fontSize = max(cmd.styleFlags & 0xFFu, 1u);

    const uint rowBits = fontRows[glyphIdx * 11u + localId.y];
    const uint col = localId.x;
    const uint row = localId.y;

    if (((rowBits >> (5u - col)) & 1u) == 0u) {
        return;
    }

    const int2 canvasSize = int2(
        int(canvasColors.get_width()),
        int(canvasColors.get_height())
    );
    const float4 color = unpackColor(cmd.colorPacked);
    const int scale = int(fontSize);

    if ((cmd.styleFlags & 0x100u) == 0u) {
        const int2 blockOrigin = basePos + int2(int(col) * scale, int(row) * scale);
        for (int dy = 0; dy < scale; ++dy) {
            for (int dx = 0; dx < scale; ++dx) {
                const int2 pixel = blockOrigin + int2(dx, dy);
                if (!isInsideCanvas(pixel, canvasSize)) {
                    continue;
                }
                canvasColors.write(color, uint2(pixel));
                canvasDistances.write(int4(int(cmd.distance), 0, 0, 0), uint2(pixel));
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
    const bool left = ((col + row) & 1u) == 0u;
    const int2 center = basePos + int2(int(col), int(row)) * scale;
    for (int k = 0; k < scale; ++k) {
        const int x = left ? (center.x + k) : (center.x + scale - 1 - k);
        for (int dy = -k; dy <= k; ++dy) {
            const int2 pixel = int2(x, center.y + dy);
            if (!isInsideCanvas(pixel, canvasSize)) {
                continue;
            }
            canvasColors.write(color, uint2(pixel));
            canvasDistances.write(int4(int(cmd.distance), 0, 0, 0), uint2(pixel));
        }
    }
}

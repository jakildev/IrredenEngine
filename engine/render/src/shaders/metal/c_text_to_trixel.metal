#include "ir_iso_common.metal"

// Per-glyph text rasterization onto the trixel canvas.  Mirrors
// shaders/c_text_to_trixel.glsl — supports a fontSize multiplier so each
// glyph cell is rendered as fontSize x fontSize trixel pixels.
//
// Workgroup dimensions: (7, 11, 1) — one thread per (col, row) of the 7x11
// glyph bitmap.

struct GlyphDrawCommand {
    uint positionPacked;  // x | (y << 16)
    uint glyphIndex;      // 0-127 ASCII index into the font atlas
    uint colorPacked;     // RGBA8 packed
    uint distance;        // depth value to stamp
    uint styleFlags;      // low byte = fontSize (pixels per glyph trixel)
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

    if (((rowBits >> (6u - col)) & 1u) == 0u) {
        return;
    }

    const int2 canvasSize = int2(
        int(canvasColors.get_width()),
        int(canvasColors.get_height())
    );
    const float4 color = unpackColor(cmd.colorPacked);
    const int2 blockOrigin =
        basePos +
        int2(int(col) * int(fontSize), int(row) * int(fontSize));

    for (uint dy = 0u; dy < fontSize; ++dy) {
        for (uint dx = 0u; dx < fontSize; ++dx) {
            const int2 pixel = blockOrigin + int2(int(dx), int(dy));
            if (!isInsideCanvas(pixel, canvasSize)) {
                continue;
            }
            canvasColors.write(color, uint2(pixel));
            canvasDistances.write(int4(int(cmd.distance), 0, 0, 0), uint2(pixel));
        }
    }
}

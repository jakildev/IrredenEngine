#version 460 core

layout(local_size_x = 7, local_size_y = 11, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Font bitmap data: fontRows[glyphIndex * 11 + row] = packed row bits
// Bit layout: bit 6 = col 0 (left), bit 0 = col 6 (right)
layout(std430, binding = 11) buffer FontBuffer {
    uint fontRows[];
};

struct GlyphDrawCommand {
    uint positionPacked;  // x | y<<16
    uint glyphIndex;      // 0-127 ASCII
    uint colorPacked;     // RGBA as uint
    uint distance;
    uint styleFlags;      // low byte = fontSize (pixels per glyph trixel)
};

layout(std430, binding = 12) buffer CommandBuffer {
    GlyphDrawCommand commands[];
};

layout(rgba8, binding = 0) writeonly uniform image2D canvasColors;
layout(r32i, binding = 1) uniform iimage2D canvasDistances;

void main() {
    GlyphDrawCommand cmd = commands[gl_WorkGroupID.x];
    ivec2 basePos = ivec2(int(cmd.positionPacked & 0xFFFFu), int(cmd.positionPacked >> 16u));
    uint glyphIdx = cmd.glyphIndex;
    uint fontSize = max(cmd.styleFlags & 0xFFu, 1u);

    uint rowBits = fontRows[glyphIdx * 11u + gl_LocalInvocationID.y];
    uint col = gl_LocalInvocationID.x;
    uint row = gl_LocalInvocationID.y;

    if (((rowBits >> (6u - col)) & 1u) == 0u) {
        return;
    }

    ivec2 canvasSize = imageSize(canvasColors);
    vec4 color = unpackColor(cmd.colorPacked);
    ivec2 blockOrigin = basePos + ivec2(int(col) * int(fontSize), int(row) * int(fontSize));

    for (uint dy = 0u; dy < fontSize; dy++) {
        for (uint dx = 0u; dx < fontSize; dx++) {
            ivec2 pixel = blockOrigin + ivec2(dx, dy);
            if (!isInsideCanvas(pixel, canvasSize)) continue;
            imageStore(canvasColors, pixel, color);
            imageStore(canvasDistances, pixel, ivec4(int(cmd.distance)));
        }
    }
}

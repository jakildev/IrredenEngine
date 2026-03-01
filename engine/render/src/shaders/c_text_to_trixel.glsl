#version 460 core

layout(local_size_x = 7, local_size_y = 11, local_size_z = 1) in;

// Font bitmap data: fontRows[glyphIndex * 11 + row] = packed row bits
// Bit layout: bit 6 = col 0 (left), bit 0 = col 6 (right)
layout(std430, binding = 11) buffer FontBuffer {
    uint fontRows[];
};

// Draw commands: one per glyph to render
// .x = positionPacked (x | y<<16)
// .y = glyphIndex (0-127 ASCII)
// .z = colorPacked (RGBA as uint)
// .w = distance
layout(std430, binding = 12) buffer CommandBuffer {
    uvec4 commands[];
};

layout(rgba8, binding = 0) writeonly uniform image2D canvasColors;
layout(r32i, binding = 1) uniform iimage2D canvasDistances;

vec4 unpackColor(uint packedColor) {
    return vec4(
        float(packedColor & 0xFFu) / 255.0,
        float((packedColor >> 8) & 0xFFu) / 255.0,
        float((packedColor >> 16) & 0xFFu) / 255.0,
        float((packedColor >> 24) & 0xFFu) / 255.0
    );
}

void main() {
    uvec4 cmd = commands[gl_WorkGroupID.x];
    ivec2 basePos = ivec2(int(cmd.x & 0xFFFFu), int(cmd.x >> 16u));
    uint glyphIdx = cmd.y;

    uint rowBits = fontRows[glyphIdx * 11u + gl_LocalInvocationID.y];
    uint col = gl_LocalInvocationID.x;

    if (((rowBits >> (6u - col)) & 1u) == 0u) {
        return;
    }

    ivec2 pixel = basePos + ivec2(int(col), int(gl_LocalInvocationID.y));

    if (pixel.x < 0 || pixel.x >= imageSize(canvasColors).x) {
        return;
    }
    if (pixel.y < 0 || pixel.y >= imageSize(canvasColors).y) {
        return;
    }

    vec4 color = unpackColor(cmd.z);
    imageStore(canvasColors, pixel, color);
    imageStore(canvasDistances, pixel, ivec4(int(cmd.w)));
}

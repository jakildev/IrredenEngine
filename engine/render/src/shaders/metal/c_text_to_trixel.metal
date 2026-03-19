#include <metal_stdlib>
using namespace metal;

float4 unpackColor(uint packedColor) {
    return float4(
        float(packedColor & 0xFFu) / 255.0,
        float((packedColor >> 8) & 0xFFu) / 255.0,
        float((packedColor >> 16) & 0xFFu) / 255.0,
        float((packedColor >> 24) & 0xFFu) / 255.0
    );
}

kernel void c_text_to_trixel(
    device const uint* fontRows [[buffer(11)]],
    device const uint4* commands [[buffer(12)]],
    texture2d<float, access::write> canvasColors [[texture(0)]],
    texture2d<int, access::write> canvasDistances [[texture(1)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    uint4 cmd = commands[groupId.x];
    int2 basePos = int2(int(cmd.x & 0xFFFFu), int(cmd.x >> 16u));
    uint glyphIdx = cmd.y;

    uint rowBits = fontRows[glyphIdx * 11u + localId.y];
    uint col = localId.x;

    if (((rowBits >> (6u - col)) & 1u) == 0u) {
        return;
    }

    int2 pixel = basePos + int2(int(col), int(localId.y));

    if (pixel.x < 0 || pixel.x >= int(canvasColors.get_width())) {
        return;
    }
    if (pixel.y < 0 || pixel.y >= int(canvasColors.get_height())) {
        return;
    }

    float4 color = unpackColor(cmd.z);
    canvasColors.write(color, uint2(pixel));
    canvasDistances.write(int4(int(cmd.w)), uint2(pixel));
}

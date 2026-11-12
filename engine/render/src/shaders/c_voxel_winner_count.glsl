#version 450 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(r32i, binding = 1) readonly uniform iimage2D canvasDistances;
layout(std430, binding = 28) readonly buffer Winners { uint winnerIds[]; };
layout(std430, binding = 24) buffer Seen { uint seenWords[]; };
layout(std430, binding = 26) buffer Counts { uint counts[]; };

void main() {
    const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(canvasDistances);
    if (any(greaterThanEqual(pixel, size))) return;
    const uint winner = winnerIds[uint(pixel.y * size.x + pixel.x)];
    if (winner == 0xFFFFFFFFu) return;
    const uint bit = 1u << (winner & 31u);
    if ((atomicOr(seenWords[winner >> 5u], bit) & bit) == 0u) {
        atomicAdd(counts[6], 1u);
    }
}

#version 450 core

// Builds the scatter occluder pyramid from the occluder prepass's depth: the
// farthest depth over each 2^L-pixel block, L = 1..5, as a code on the 2^-24
// depth lattice rounded toward the far plane. The atlas layout is in
// ir_per_axis_occluder_layout.glsl and the reader in
// ir_per_axis_occlusion.glsl. Metal mirror in
// metal/c_per_axis_occluder_reduce.metal.
//
// One invocation owns one 32-pixel block and writes every level's texels
// inside it, so no level is read back and the pass needs no barrier between
// levels. Dispatched over ceil(target / 32) groups. The four nested quadrant
// loops are the five levels: kOccluderLevels must stay 5.

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

#include "ir_per_axis_occluder_layout.glsl"

layout(binding = 0) uniform sampler2D occluderPrepassDepth;
layout(r32i, binding = 1) writeonly uniform iimage2D occluderPyramid;

// Farthest code over the 2x2 pixels at @p pixel that lie inside the target,
// or -1 when none does.
int farthestCode2x2(ivec2 pixel, ivec2 size) {
    int code = -1;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            const ivec2 texel = pixel + ivec2(dx, dy);
            if (texel.x < size.x && texel.y < size.y) {
                const float depth = clamp(texelFetch(occluderPrepassDepth, texel, 0).r, 0.0, 1.0);
                code = max(code, int(ceil(depth * kOccluderDepthCodes)));
            }
        }
    }
    return code;
}

ivec2 quadrant(int index) {
    return ivec2(index & 1, index >> 1);
}

void main() {
    const ivec2 size = textureSize(occluderPrepassDepth, 0);
    const ivec2 block = ivec2(gl_GlobalInvocationID.xy) << 5;
    if (block.x >= size.x || block.y >= size.y) {
        return;
    }
    const ivec2 origin1 = occluderLevelOrigin(1, size);
    const ivec2 origin2 = occluderLevelOrigin(2, size);
    const ivec2 origin3 = occluderLevelOrigin(3, size);
    const ivec2 origin4 = occluderLevelOrigin(4, size);
    const ivec2 origin5 = occluderLevelOrigin(5, size);

    int code5 = -1;
    for (int q4 = 0; q4 < 4; ++q4) {
        const ivec2 pixel4 = block + (quadrant(q4) << 4);
        int code4 = -1;
        for (int q3 = 0; q3 < 4; ++q3) {
            const ivec2 pixel3 = pixel4 + (quadrant(q3) << 3);
            int code3 = -1;
            for (int q2 = 0; q2 < 4; ++q2) {
                const ivec2 pixel2 = pixel3 + (quadrant(q2) << 2);
                int code2 = -1;
                for (int q1 = 0; q1 < 4; ++q1) {
                    const ivec2 pixel1 = pixel2 + (quadrant(q1) << 1);
                    const int code1 = farthestCode2x2(pixel1, size);
                    if (code1 >= 0) {
                        imageStore(occluderPyramid, origin1 + (pixel1 >> 1), ivec4(code1, 0, 0, 0));
                    }
                    code2 = max(code2, code1);
                }
                if (code2 >= 0) {
                    imageStore(occluderPyramid, origin2 + (pixel2 >> 2), ivec4(code2, 0, 0, 0));
                }
                code3 = max(code3, code2);
            }
            if (code3 >= 0) {
                imageStore(occluderPyramid, origin3 + (pixel3 >> 3), ivec4(code3, 0, 0, 0));
            }
            code4 = max(code4, code3);
        }
        if (code4 >= 0) {
            imageStore(occluderPyramid, origin4 + (pixel4 >> 4), ivec4(code4, 0, 0, 0));
        }
        code5 = max(code5, code4);
    }
    imageStore(occluderPyramid, origin5 + (block >> 5), ivec4(code5, 0, 0, 0));
}

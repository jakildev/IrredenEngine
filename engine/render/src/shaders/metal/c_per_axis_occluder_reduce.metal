// Mirror of shaders/c_per_axis_occluder_reduce.glsl; keep the two in lockstep.
//
// Builds the scatter occluder pyramid from the occluder prepass's depth: the
// farthest depth over each 2^L-pixel block, L = 1..5, as a code on the 2^-24
// depth lattice rounded toward the far plane. The atlas layout is in
// ir_per_axis_occluder_layout.metal and the reader in
// ir_per_axis_occlusion.metal.
//
// One thread owns one 32-pixel block and writes every level's texels inside
// it, so no level is read back and the pass needs no barrier between levels.
// Dispatched over ceil(target / 32) groups. The four nested quadrant loops are
// the five levels: kOccluderLevels must stay 5.

#include "ir_per_axis_occluder_layout.metal"

// Farthest code over the 2x2 pixels at @p pixel that lie inside the target,
// or -1 when none does.
static inline int farthestCode2x2(
    int2 pixel, int2 size, depth2d<float, access::read> occluderPrepassDepth
) {
    int code = -1;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            const int2 texel = pixel + int2(dx, dy);
            if (texel.x < size.x && texel.y < size.y) {
                const float depth = clamp(occluderPrepassDepth.read(uint2(texel)), 0.0f, 1.0f);
                code = max(code, int(ceil(depth * kOccluderDepthCodes)));
            }
        }
    }
    return code;
}

static inline int2 quadrant(int index) {
    return int2(index & 1, index >> 1);
}

kernel void c_per_axis_occluder_reduce(
    depth2d<float, access::read> occluderPrepassDepth [[texture(0)]],
    texture2d<int, access::write> occluderPyramid [[texture(1)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 size = int2(occluderPrepassDepth.get_width(), occluderPrepassDepth.get_height());
    const int2 block = int2(globalId.xy) << 5;
    if (block.x >= size.x || block.y >= size.y) {
        return;
    }
    const int2 origin1 = occluderLevelOrigin(1, size);
    const int2 origin2 = occluderLevelOrigin(2, size);
    const int2 origin3 = occluderLevelOrigin(3, size);
    const int2 origin4 = occluderLevelOrigin(4, size);
    const int2 origin5 = occluderLevelOrigin(5, size);

    int code5 = -1;
    for (int q4 = 0; q4 < 4; ++q4) {
        const int2 pixel4 = block + (quadrant(q4) << 4);
        int code4 = -1;
        for (int q3 = 0; q3 < 4; ++q3) {
            const int2 pixel3 = pixel4 + (quadrant(q3) << 3);
            int code3 = -1;
            for (int q2 = 0; q2 < 4; ++q2) {
                const int2 pixel2 = pixel3 + (quadrant(q2) << 2);
                int code2 = -1;
                for (int q1 = 0; q1 < 4; ++q1) {
                    const int2 pixel1 = pixel2 + (quadrant(q1) << 1);
                    const int code1 = farthestCode2x2(pixel1, size, occluderPrepassDepth);
                    if (code1 >= 0) {
                        occluderPyramid.write(int4(code1, 0, 0, 0), uint2(origin1 + (pixel1 >> 1)));
                    }
                    code2 = max(code2, code1);
                }
                if (code2 >= 0) {
                    occluderPyramid.write(int4(code2, 0, 0, 0), uint2(origin2 + (pixel2 >> 2)));
                }
                code3 = max(code3, code2);
            }
            if (code3 >= 0) {
                occluderPyramid.write(int4(code3, 0, 0, 0), uint2(origin3 + (pixel3 >> 3)));
            }
            code4 = max(code4, code3);
        }
        if (code4 >= 0) {
            occluderPyramid.write(int4(code4, 0, 0, 0), uint2(origin4 + (pixel4 >> 4)));
        }
        code5 = max(code5, code4);
    }
    occluderPyramid.write(int4(code5, 0, 0, 0), uint2(origin5 + (block >> 5)));
}

#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_build_distance_hiz.glsl. Hi-Z (hierarchical max-depth)
// downsample for voxel occlusion culling (#1294 child 1/3). One dispatch per
// mip level: reads the previous distance level, writes each destination texel
// the MAX of its (up to) 2x2 source footprint. See the GLSL for the
// conservative-max rationale, the raw-encoded-int max justification, and the
// ceil-division coverage guarantee that makes the clamp on the +1 taps safe.

kernel void c_build_distance_hiz(
    texture2d<int, access::read> srcDistances [[texture(0)]],
    texture2d<int, access::write> dstHiZ [[texture(1)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 dst = int2(globalId.xy);
    int2 dstSize = int2(int(dstHiZ.get_width()), int(dstHiZ.get_height()));
    if (dst.x >= dstSize.x || dst.y >= dstSize.y) return;

    int2 srcSize = int2(int(srcDistances.get_width()), int(srcDistances.get_height()));
    int2 maxCoord = srcSize - int2(1);
    int2 base = dst * 2;

    int m = srcDistances.read(uint2(clamp(base, int2(0), maxCoord))).x;
    m = max(m, srcDistances.read(uint2(clamp(base + int2(1, 0), int2(0), maxCoord))).x);
    m = max(m, srcDistances.read(uint2(clamp(base + int2(0, 1), int2(0), maxCoord))).x);
    m = max(m, srcDistances.read(uint2(clamp(base + int2(1, 1), int2(0), maxCoord))).x);

    dstHiZ.write(int4(m, 0, 0, 0), uint2(dst));
}

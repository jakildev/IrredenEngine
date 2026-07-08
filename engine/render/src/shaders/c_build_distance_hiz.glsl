#version 450 core

// Hi-Z (hierarchical max-depth) downsample for voxel occlusion culling
// (#1294 child 1/3; docs/design/voxel-occlusion-culling.md § Implementation
// sketch step 1). One dispatch per mip level: reads the previous level's
// distance image and writes each destination texel the MAX (farthest) of its
// (up to) 2x2 source footprint. Conceptual level 0 is the canvas
// trixelDistances; this pass produces the downsampled levels on
// C_TriangleCanvasTextures::hiZMips_.
//
// Why MAX: a coarse texel answers "the farthest visible surface over this
// footprint". The chunk-occlusion pre-pass (child 2) culls a pool-chunk only
// when its NEAREST depth is strictly behind that max — i.e. closer geometry
// covers the whole footprint. The empty/background sentinel (65535) is the
// largest encoded value, so any footprint still seeing background keeps the
// max at 65535 = "never occlude" — the conservative direction (a false
// positive is a visible hole; a false negative is only lost savings).
//
// The raw R32I encoded distance is max'd directly: the main-canvas encoding
// packs depth in bits [31:3] (flip at [2], face slot in [1:0], #2207), so larger encoded value
// <=> farther, and an integer max over encoded values is a faithful max-depth
// pyramid. Child 2 compares raw encoded values consistently on both sides.
//
// Ceil-division destination sizing (CPU side, makeHiZMipChain) guarantees every
// source texel maps into exactly one destination texel's 2x2 block, so the
// clamp on the +1 taps only ever re-reads an in-bounds texel (harmless for max)
// and no source texel is dropped from the reduction at odd source dimensions.
//
// Mirror: shaders/metal/c_build_distance_hiz.metal.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(r32i, binding = 0) readonly uniform iimage2D srcDistances;
layout(r32i, binding = 1) writeonly uniform iimage2D dstHiZ;

void main() {
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dstSize = imageSize(dstHiZ);
    if (dst.x >= dstSize.x || dst.y >= dstSize.y) return;

    ivec2 srcSize = imageSize(srcDistances);
    ivec2 maxCoord = srcSize - ivec2(1);
    ivec2 base = dst * 2;

    int m = imageLoad(srcDistances, clamp(base, ivec2(0), maxCoord)).x;
    m = max(m, imageLoad(srcDistances, clamp(base + ivec2(1, 0), ivec2(0), maxCoord)).x);
    m = max(m, imageLoad(srcDistances, clamp(base + ivec2(0, 1), ivec2(0), maxCoord)).x);
    m = max(m, imageLoad(srcDistances, clamp(base + ivec2(1, 1), ivec2(0), maxCoord)).x);

    imageStore(dstHiZ, dst, ivec4(m, 0, 0, 0));
}

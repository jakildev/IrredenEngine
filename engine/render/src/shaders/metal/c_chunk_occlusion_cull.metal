#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_chunk_occlusion_cull.glsl. The Hi-Z is last frame's, so a
// cull is only ever strictly-behind that frame's max. Only chunks the CPU
// flagged `eligible_` are tested, and a footprint that still sees background
// (the 65535 sentinel, the largest encoded value) is never culled.

constant constexpr int kMaxHiZMipLevels = 12;
// One full depth step at the kDepthEncodeShift = 8 encode scale.
constant constexpr int kOcclusionDepthMargin = 8;

// Matches the std430 ChunkQuery (32 B). Record 0 of the buffer is the header:
// header.pixelMin_.x = chunkCount, header.pixelMin_.y = mipCount.
struct ChunkQuery {
    int2 pixelMin_;
    int2 pixelMax_;
    int encodedNearest_;
    int eligible_;
    int pad0_;
    int pad1_;
};

// Metal allows dynamic indexing of the texture array, so no constant-index
// ladder is needed.
static int hiZTexel(
    array<texture2d<int, access::read>, kMaxHiZMipLevels> hiZLevels,
    int level,
    int2 coord
) {
    int l = clamp(level, 0, kMaxHiZMipLevels - 1);
    int2 sz = int2(int(hiZLevels[l].get_width()), int(hiZLevels[l].get_height()));
    int2 c = clamp(coord, int2(0), sz - int2(1));
    return hiZLevels[l].read(uint2(c)).x;
}

// Smallest downsampled level whose texel footprint (2^(idx+1) source px) covers
// `footprintPx`, as a 0-based index into hiZLevels (conceptual mip idx+1).
static int pickHiZLevel(int footprintPx, int mipCount) {
    int idx = 0;
    while (idx < mipCount - 1 && (1 << (idx + 1)) < footprintPx) {
        ++idx;
    }
    return clamp(idx, 0, mipCount - 1);
}

kernel void c_chunk_occlusion_cull(
    device const ChunkQuery *records [[buffer(25)]],
    device atomic_uint *chunkVisible [[buffer(24)]],
    array<texture2d<int, access::read>, kMaxHiZMipLevels> hiZLevels [[texture(0)]],
    uint3 tgPos [[threadgroup_position_in_grid]],
    uint3 tgPerGrid [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    int chunkCount = records[0].pixelMin_.x;
    int mipCount = records[0].pixelMin_.y;

    uint workGroupIndex = tgPos.x + tgPos.y * tgPerGrid.x;
    int chunk = int(workGroupIndex * 64u + localId.x);
    if (chunk >= chunkCount) return;
    if (mipCount <= 0) return;

    // chunkVisible is read-modify; relaxed atomics keep parity with the GLSL
    // plain store (one thread owns each chunk slot, so there is no real race).
    if (atomic_load_explicit(&chunkVisible[chunk], memory_order_relaxed) == 0u) return;

    ChunkQuery q = records[1 + chunk];
    if (q.eligible_ == 0) return;

    int2 footprint = q.pixelMax_ - q.pixelMin_;
    int footprintPx = max(max(footprint.x, footprint.y), 1);
    int level = pickHiZLevel(footprintPx, mipCount);

    int shift = level + 1;
    int2 tMin = (q.pixelMin_ >> shift) - int2(1);
    int2 tMax = (q.pixelMax_ >> shift) + int2(1);

    int hiZMax = -2147483648;
    for (int ty = tMin.y; ty <= tMax.y; ++ty) {
        for (int tx = tMin.x; tx <= tMax.x; ++tx) {
            hiZMax = max(hiZMax, hiZTexel(hiZLevels, level, int2(tx, ty)));
        }
    }

    if (q.encodedNearest_ > hiZMax + kOcclusionDepthMargin) {
        atomic_store_explicit(&chunkVisible[chunk], 0u, memory_order_relaxed);
    }
}

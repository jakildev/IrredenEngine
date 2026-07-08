#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_chunk_occlusion_cull.glsl. Chunk-occlusion pre-pass for the
// voxel-pool render path (#1294 child 2/3). One thread per pool-chunk: sample
// the MAX of last frame's Hi-Z (#1798) over the chunk's projected pixel
// footprint and AND 0 into the chunk's ChunkVisibility entry (buffer 24) iff the
// chunk's nearest depth is strictly behind that max. See the GLSL for the
// conservative-eligibility rules, the background-sentinel rationale, and why the
// pass is gated off by default.

constant constexpr int kMaxHiZMipLevels = 12;
// One full depth step at the kDepthEncodeShift = 8 encode scale (GLSL twin).
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

// Fetch one Hi-Z texel at `level` (clamped in-bounds). Metal allows dynamic
// indexing of the texture array, so no constant-index ladder is needed.
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

    // Linear chunk index from the 2D threadgroup grid — mirrors the GLSL.
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

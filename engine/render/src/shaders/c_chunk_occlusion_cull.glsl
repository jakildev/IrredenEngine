#version 450 core

// Chunk-occlusion pre-pass for the voxel-pool render path (#1294 child 2/3;
// docs/design/voxel-occlusion-culling.md § Implementation sketch step 2).
//
// One invocation per pool-chunk. Each chunk's iso AABB is projected to canvas
// pixels CPU-side and delivered in the query buffer alongside the chunk's
// nearest encoded depth. This pass samples the MAX of LAST frame's Hi-Z
// (#1798) over the chunk's pixel footprint, at the mip whose texel covers the
// footprint, and ANDs 0 into the chunk's ChunkVisibility entry (binding 24)
// iff the chunk's nearest depth is strictly behind that max — i.e. closer
// geometry already covers the whole footprint.
//
// Conservative by construction:
//   * Only chunks the CPU flagged `eligible_` are testable (NONE render mode,
//     cardinal yaw, fully inside the VISIBLE viewport so never an off-screen
//     shadow feeder). Everything else keeps its frustum visibility bit.
//   * A footprint that still sees background keeps the chunk: the empty/
//     background texel carries the 65535 sentinel — the largest encoded value
//     in NONE mode — so the max stays at 65535 = "never occlude". A false
//     positive is a visible hole; a false negative is only lost compute.
//   * The footprint is expanded by one texel and the cull only fires on a
//     strict-behind compare with a small margin.
//
// The whole pass is gated OFF by default at the call site (it is not dispatched
// unless the occlusion cull is enabled), so a default scene is byte-identical
// to master and pays zero cost.
//
// Mirror: shaders/metal/c_chunk_occlusion_cull.metal.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Hi-Z downsampled levels (conceptual mip 1..N) bound as sampled R32I images.
// The CPU binds [0, mipCount_) to the real levels and any surplus units to the
// coarsest level (never sampled — mip selection clamps to mipCount_ - 1). 12
// covers up to an 8192px canvas (ceil(log2(8192)) downsampled levels).
const int kMaxHiZMipLevels = 12;

// Array element i reads texture unit (0 + i); the CPU binds each Hi-Z level to
// the matching unit via Texture2D::bind(i).
layout(binding = 0) uniform isampler2D hiZLevels[kMaxHiZMipLevels];

// Per-chunk query, authored CPU-side (system_voxel_to_trixel.hpp). 32 B, 16-byte
// aligned so the std430 runtime array stride matches the C++ struct.
struct ChunkQuery {
    ivec2 pixelMin_;     // chunk iso AABB projected to canvas pixels (inclusive)
    ivec2 pixelMax_;
    int encodedNearest_; // chunk nearest depth, encoded like trixelDistances (rawDepth*8)
    int eligible_;       // 1 = testable this frame; 0 = always keep
    int pad0_;
    int pad1_;
};

// A 32-byte header (matching the ChunkQuery stride) then the per-chunk array,
// so queries[] starts on a 32-byte boundary and the Metal mirror can index the
// whole buffer as one ChunkQuery[] (record 0 = header). Bound transiently on
// kBufferIndex_CompactedVoxelIndices (25) for this dispatch only — the compact
// pass rebinds the real compacted-index buffer afterward.
layout(std430, binding = 25) readonly buffer ChunkOcclusionQueryBuffer {
    int chunkCount_;
    int mipCount_;
    ivec2 _headerPad0_;
    ivec4 _headerPad1_;
    ChunkQuery queries[];
};

// Read-write here (the compact pass declares the same binding readonly).
layout(std430, binding = 24) buffer ChunkVisibility {
    uint chunkVisible[];
};

// Strict-behind margin (in encoded units — one full depth step at the
// kDepthEncodeShift = 8 encode scale) so FMA / round noise on the boundary
// never culls a chunk that is only coplanar with the occluder.
const int kOcclusionDepthMargin = 8;

// Fetch one Hi-Z texel at `level` (clamped in-bounds). A constant-index ladder
// keeps the sampler-array access dynamically-uniform-safe in portable GLSL —
// the per-chunk `level` is not dynamically uniform, so hiZLevels[level] would
// be UB without it.
int hiZTexel(int level, ivec2 coord) {
    ivec2 sz;
#define IR_HIZ_TAP(L)                                                                   \
    sz = textureSize(hiZLevels[L], 0);                                                  \
    return texelFetch(hiZLevels[L], clamp(coord, ivec2(0), sz - ivec2(1)), 0).x;
    if (level <= 0) { IR_HIZ_TAP(0) }
    else if (level == 1) { IR_HIZ_TAP(1) }
    else if (level == 2) { IR_HIZ_TAP(2) }
    else if (level == 3) { IR_HIZ_TAP(3) }
    else if (level == 4) { IR_HIZ_TAP(4) }
    else if (level == 5) { IR_HIZ_TAP(5) }
    else if (level == 6) { IR_HIZ_TAP(6) }
    else if (level == 7) { IR_HIZ_TAP(7) }
    else if (level == 8) { IR_HIZ_TAP(8) }
    else if (level == 9) { IR_HIZ_TAP(9) }
    else if (level == 10) { IR_HIZ_TAP(10) }
    else { IR_HIZ_TAP(11) }
#undef IR_HIZ_TAP
}

// Smallest downsampled level whose texel footprint (2^(idx+1) source px) covers
// `footprintPx`, as a 0-based index into hiZLevels (conceptual mip idx+1).
int pickHiZLevel(int footprintPx, int mipCount) {
    int idx = 0;
    // texelSpan(idx) = 2^(idx+1) source pixels.
    while (idx < mipCount - 1 && (1 << (idx + 1)) < footprintPx) {
        ++idx;
    }
    return clamp(idx, 0, mipCount - 1);
}

void main() {
    // Linear chunk index from the 2D workgroup grid (voxelDispatchGridForCount,
    // same scheme as c_voxel_visibility_compact) so chunk counts past one grid
    // row are covered.
    uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    uint chunk = workGroupIndex * 64u + gl_LocalInvocationID.x;
    if (chunk >= uint(chunkCount_)) return;
    if (mipCount_ <= 0) return;

    // Already culled by frustum, or flagged not-testable this frame — leave it.
    if (chunkVisible[chunk] == 0u) return;
    ChunkQuery q = queries[chunk];
    if (q.eligible_ == 0) return;

    ivec2 footprint = q.pixelMax_ - q.pixelMin_;
    int footprintPx = max(max(footprint.x, footprint.y), 1);
    int level = pickHiZLevel(footprintPx, mipCount_);

    // Project the pixel AABB into this level's texel grid (level idx maps source
    // px -> px >> (idx+1)), expand by one texel (conservative), and take the max
    // over that small footprint.
    int shift = level + 1;
    ivec2 tMin = (q.pixelMin_ >> shift) - ivec2(1);
    ivec2 tMax = (q.pixelMax_ >> shift) + ivec2(1);

    int hiZMax = -2147483648;
    for (int ty = tMin.y; ty <= tMax.y; ++ty) {
        for (int tx = tMin.x; tx <= tMax.x; ++tx) {
            hiZMax = max(hiZMax, hiZTexel(level, ivec2(tx, ty)));
        }
    }

    // Strictly behind the farthest visible surface over the whole footprint =
    // covered by closer geometry. Background (65535) keeps hiZMax large, so a
    // footprint that still sees background is never culled.
    if (q.encodedNearest_ > hiZMax + kOcclusionDepthMargin) {
        chunkVisible[chunk] = 0u;
    }
}

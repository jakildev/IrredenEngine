#version 450 core

// Canonical-order the view-visibility overflow entry list (#2479).
//
// The mode-3 append (c_voxel_to_trixel_stage_1_body.glsl) assigns entry
// indices with atomicAdd, and entry index IS draw order in the overflow
// scatter branch (v_peraxis_scatter.glsl indexes by gl_InstanceID), so
// equal-key entries resolve their depth contest by run-variant arrival
// order — 10 distinct rotated-shot hashes across 10 runs on the amplitude-5
// displaced scene. This pass in-place bitonic-sorts the appended 3-word
// entries by full record value between the append and the indirect draw,
// making draw order — and therefore every equal-key winner — a pure
// function of the appended SET.
//
// Sort key: lexicographic (packedCell, encodedDistance, colorPacked) =
// entry words (0, 2, 1). Any total value order works; this one groups
// cell ties adjacently, which keeps the both-sentinel early-out effective.
//
// Pass structure (overflowSortStep.x, driven by system_voxel_to_trixel.hpp;
// the entry cap is a power of two by construction — see
// component_per_axis_trixel_canvases.hpp):
//   0 — sentinel-fill every slot in [liveCount, cap) with 0xFFFFFFFF^3. The
//       region above the live range holds stale prior-frame entries, NOT
//       zeros — the fill is mandatory, every rotating frame, before any
//       network step. A real entry cannot tie the sentinel: word 0 packs
//       canvas cell x|y at 16+16 bits and canvas cell coordinates never
//       reach 65535, so a real all-max key is unproducible.
//   1 — fused local phase: full bitonic sort of each contiguous
//       kBlock-element block in shared memory (stages k = 2..kBlock).
//   2 — one global compare-exchange step of stage k at stride j (j >= half
//       of kBlock's successor — the strides too wide to fuse).
//   3 — fused local tail of stage k: the remaining strides
//       (kBlock/2 .. 1) of one stage, in shared memory.
// Out-of-live-range lanes are sentinel-SUBSTITUTED, never thread-skipped:
// every lane participates in its compare-exchange so the network stays
// valid. The only shortcut is the value-based both-sentinel early-out in
// mode 2 (a +inf/+inf pair is provably a no-op). The sort never touches the
// ctrl block (draw args / counters) — it reorders entries, nothing else.

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

// Prefix of FrameDataVoxelToCanvas (binding 7) through overflowSortStep_
// (offset 224). Only the fields this pass reads are named; every other field
// is padded so the std140 offsets stay in lockstep with the C++ struct (the
// c_light_overflow_faces.glsl prefix, extended one ivec4).
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    vec2  _frameCanvasOffset;
    ivec2 _trixelCanvasOffsetZ1;
    ivec2 _voxelRenderOptions;
    ivec2 _voxelDispatchGrid;
    int   _voxelCount;
    int   _perAxisRoute;
    ivec2 _canvasSizePixels;
    ivec2 _cullIsoMin;
    ivec2 _cullIsoMax;
    float _visualYaw;
    float _rasterYaw;
    float _residualYaw;
    float _isDetachedCanvas;
    vec4  _faceDeformPadding[3];
    ivec4 _visibleFaceIds;
    vec4  _voxelDepthAxisUnused;
    vec4  _detachedWorldReceive;
    ivec4 _visibleIsoBounds;
    int   _resolveMode;
    int   _occlusionCullMipCount;
    int   _feederSubCap;
    int   _feederPassTailBase;
    ivec4 overflowScratchLayout;   // .y ctrl base, .z entry base, .w cap
    ivec4 overflowSortStep;        // .x mode, .y stage k, .z stride j
};

// The unified per-axis resolve scratch, bound whole at 28 during the
// per-axis window. Plain (non-atomic) access is safe here: every dispatch of
// this pass runs behind a storage barrier on the append (and on the prior
// network step), mirroring c_resolve_per_axis_blit's post-barrier contract.
layout(std430, binding = 28) buffer PerAxisResolveScratch {
    uint scratch[];
};

const uint kSentinelWord = 0xFFFFFFFFu;
// Elements per fused-local block: 256 threads x 2 elements. 3 shared words
// per element = 6 KB, comfortably under the 32 KB shared-memory floor.
const uint kBlock = 512u;

shared uint sW0[kBlock];
shared uint sW1[kBlock];
shared uint sW2[kBlock];

uint entriesBase() {
    return uint(overflowScratchLayout.z);
}

uint capEntries() {
    return uint(overflowScratchLayout.w);
}

uint liveEntryCount() {
    // ctrl[1] is the indirect-draw instanceCount the append settled; the cap
    // clamp guards the (already paired-back) transient over-cap value.
    return min(scratch[uint(overflowScratchLayout.y) + 1u], capEntries());
}

// Lexicographic (cell, distance, color) — words (0, 2, 1).
bool recordLess(uint a0, uint a1, uint a2, uint b0, uint b1, uint b2) {
    if (a0 != b0) return a0 < b0;
    if (a2 != b2) return a2 < b2;
    return a1 < b1;
}

// Compare-exchange the shared-memory records at elements i and i+j toward
// @p ascending order.
void localCompareExchange(uint i, uint j, bool ascending) {
    const uint p = i + j;
    const bool wrongOrder =
        recordLess(sW0[p], sW1[p], sW2[p], sW0[i], sW1[i], sW2[i]) == ascending;
    if (!wrongOrder) return;
    uint t0 = sW0[i]; uint t1 = sW1[i]; uint t2 = sW2[i];
    sW0[i] = sW0[p]; sW1[i] = sW1[p]; sW2[i] = sW2[p];
    sW0[p] = t0; sW1[p] = t1; sW2[p] = t2;
}

void loadBlockToShared(uint blockBase, uint t) {
    for (uint e = t; e < kBlock; e += 256u) {
        const uint b = entriesBase() + (blockBase + e) * 3u;
        sW0[e] = scratch[b];
        sW1[e] = scratch[b + 1u];
        sW2[e] = scratch[b + 2u];
    }
}

void storeBlockFromShared(uint blockBase, uint t) {
    for (uint e = t; e < kBlock; e += 256u) {
        const uint b = entriesBase() + (blockBase + e) * 3u;
        scratch[b] = sW0[e];
        scratch[b + 1u] = sW1[e];
        scratch[b + 2u] = sW2[e];
    }
}

void main() {
    const uint mode = uint(overflowSortStep.x);

    if (mode == 0u) {
        // Sentinel fill of [liveCount, cap).
        const uint i = gl_GlobalInvocationID.x;
        if (i >= capEntries() || i < liveEntryCount()) return;
        const uint b = entriesBase() + i * 3u;
        scratch[b] = kSentinelWord;
        scratch[b + 1u] = kSentinelWord;
        scratch[b + 2u] = kSentinelWord;
        return;
    }

    if (mode == 2u) {
        // One global step of stage k at stride j: thread p owns the pair
        // (i, i+j) with i = 2j*(p/j) + (p%j); direction from the global
        // element index's k bit (the classic ascending-overall network).
        const uint k = uint(overflowSortStep.y);
        const uint j = uint(overflowSortStep.z);
        const uint p = gl_GlobalInvocationID.x;
        if (p >= capEntries() >> 1u) return;
        const uint i = 2u * j * (p / j) + (p % j);
        const uint bi = entriesBase() + i * 3u;
        const uint bp = entriesBase() + (i + j) * 3u;
        const uint a0 = scratch[bi];
        const uint b0 = scratch[bp];
        // Both +inf: provably a no-op exchange (the one sanctioned shortcut —
        // per-pass traffic scales with the live count, the pass structure
        // doesn't change).
        if (a0 == kSentinelWord && b0 == kSentinelWord) return;
        const uint a1 = scratch[bi + 1u];
        const uint a2 = scratch[bi + 2u];
        const uint b1 = scratch[bp + 1u];
        const uint b2 = scratch[bp + 2u];
        const bool ascending = (i & k) == 0u;
        if ((recordLess(b0, b1, b2, a0, a1, a2)) != ascending) return;
        scratch[bi] = b0; scratch[bi + 1u] = b1; scratch[bi + 2u] = b2;
        scratch[bp] = a0; scratch[bp + 1u] = a1; scratch[bp + 2u] = a2;
        return;
    }

    // Modes 1 and 3: fused shared-memory phases over one kBlock-element block.
    const uint blockBase = gl_WorkGroupID.x * kBlock;
    const uint t = gl_LocalInvocationID.x;

    if (mode == 1u && blockBase >= liveEntryCount()) {
        // Pre-network state: every element of this block is the just-filled
        // sentinel, and sorting a constant block is a no-op. Workgroup-uniform
        // condition, so the barriers below stay in uniform control flow.
        return;
    }

    loadBlockToShared(blockBase, t);
    barrier();

    if (mode == 1u) {
        // Full local bitonic: stages k = 2..kBlock, all strides. Direction
        // comes from the GLOBAL element index so these fused stages compose
        // exactly with the global steps above.
        for (uint k = 2u; k <= kBlock; k <<= 1u) {
            for (uint j = k >> 1u; j >= 1u; j >>= 1u) {
                const uint i = 2u * j * (t / j) + (t % j);
                localCompareExchange(i, j, ((blockBase + i) & k) == 0u);
                barrier();
            }
        }
    } else {
        // Mode 3 — local tail of stage k: strides kBlock/2 .. 1. k >= 2*kBlock
        // here, and blockBase is a multiple of kBlock, so the direction bit is
        // constant across the block.
        const uint k = uint(overflowSortStep.y);
        const bool ascending = (blockBase & k) == 0u;
        for (uint j = kBlock >> 1u; j >= 1u; j >>= 1u) {
            const uint i = 2u * j * (t / j) + (t % j);
            localCompareExchange(i, j, ascending);
            barrier();
        }
    }

    storeBlockFromShared(blockBase, t);
}

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
// cell ties adjacently. The key spans ALL THREE words, so key-equality and
// record-equality coincide — which is why the compare-exchange below can be
// strict (equal records never swap) without making draw order depend on
// arrival order: two fully-equal records are indistinguishable in the output.
//
// Pass structure (overflowSortStep, driven by system_voxel_to_trixel.hpp;
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
//   2 — fused STRIDED-SLAB phase: every compare-exchange stride of stage k
//       whose bit position falls in [pLo, pHi] (at most kBlockBits of them),
//       run entirely in shared memory.
//
// The strided slab (#2479 revision v3, the architect's option-A fusion) is
// what keeps the dispatch count off the encoder-round-trip cliff: Metal
// creates one compute encoder per dispatch with a full resource-table flush
// (~40 us), and memoryBarrier() is a no-op there because encoder boundaries
// already serialize — so the unfused network's 67 dispatches at the repro
// scene's 524,288-entry cap cost ~+2.9 ms/rotating frame in encoder overhead
// alone, not GPU arithmetic. A slab of kBlock elements closed under a
// contiguous RANGE of stride bit positions fuses up to kBlockBits strides per
// dispatch, cutting the same network to 18 dispatches.
//
// Slab addressing: for stride bit positions [pLo, pHi] (n = pHi - pLo + 1),
// the elements that compare against each other are exactly those differing
// only in bits [pLo, pHi] — 2^n of them. `c` is the COMPRESSED index over
// every OTHER bit of the global entry index, so
//     i = ((c >> pLo) << (pHi + 1)) | (active << pLo) | (c & ((1 << pLo) - 1))
// reinserts the active window. A workgroup owns kBlock >> n consecutive
// slabs, hence kBlock elements and cap/kBlock workgroups for EVERY stride
// group regardless of its width. pLo == 0 degenerates to a contiguous block,
// which is why this one mode also covers the local tail of each stage.
//
// Out-of-live-range lanes are sentinel-SUBSTITUTED, never thread-skipped:
// every lane participates in its compare-exchange so the network stays
// valid. The only shortcut is the workgroup-uniform whole-block-is-sentinel
// early-out, taken only where the slab is contiguous (mode 1, and mode 2 at
// pLo == 0) so a sorted-constant block is provably a no-op. The sort never
// touches the ctrl block (draw args / counters) — it reorders entries,
// nothing else.
//
// The whole pass is gated CPU-side on the pool's storeTiesPossible_ flag
// (#2346), so an unflagged pool dispatches NONE of these modes.

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
    ivec4 overflowSortStep;        // .x mode, .y stage k, .z pLo, .w pHi
};

// The unified per-axis resolve scratch, bound whole at 28 during the
// per-axis window. Plain (non-atomic) access is safe here: every dispatch of
// this pass runs behind a storage barrier on the append (and on the prior
// network step), mirroring c_resolve_per_axis_blit's post-barrier contract.
layout(std430, binding = 28) buffer PerAxisResolveScratch {
    uint scratch[];
};

const uint kSentinelWord = 0xFFFFFFFFu;
const uint kThreads = 256u;
// Elements per fused slab. 3 shared words per element = 24 KB, the largest
// power of two under the 32 KB shared-memory floor both backends guarantee —
// and therefore the widest stride range a single dispatch can fuse.
// kBlockBits MUST match the kSortBlockBits constant in the Metal twin and
// the kBlockBits literal in system_voxel_to_trixel.hpp.
const uint kBlockBits = 11u;
const uint kBlock = 1u << kBlockBits;

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

// Reinsert the active-bit window into a compressed slab index.
uint expandIndex(uint c, uint active, uint pLo, uint pHi) {
    return ((c >> pLo) << (pHi + 1u)) | (active << pLo) | (c & ((1u << pLo) - 1u));
}

// Compare-exchange the shared-memory records at elements e and e+stride
// toward @p ascending order. Strict on both sides: equal records never swap,
// so a sentinel-saturated slab performs zero shared-memory writes.
void localCompareExchange(uint e, uint stride, bool ascending) {
    const uint p = e + stride;
    const bool swapNeeded = ascending
        ? recordLess(sW0[p], sW1[p], sW2[p], sW0[e], sW1[e], sW2[e])
        : recordLess(sW0[e], sW1[e], sW2[e], sW0[p], sW1[p], sW2[p]);
    if (!swapNeeded) return;
    uint t0 = sW0[e]; uint t1 = sW1[e]; uint t2 = sW2[e];
    sW0[e] = sW0[p]; sW1[e] = sW1[p]; sW2[e] = sW2[p];
    sW0[p] = t0; sW1[p] = t1; sW2[p] = t2;
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

    const uint t = gl_LocalInvocationID.x;
    const uint g = gl_WorkGroupID.x;
    const uint pairsPerBlock = kBlock >> 1u;

    if (mode == 1u) {
        // Full local bitonic over one contiguous kBlock-element block:
        // stages k = 2..kBlock, all strides. Direction comes from the GLOBAL
        // element index so these fused stages compose exactly with the
        // strided-slab steps below.
        const uint blockBase = g * kBlock;
        if (blockBase >= liveEntryCount()) {
            // Pre-network state: every element of this block is the just-filled
            // sentinel, and sorting a constant block is a no-op. Workgroup-
            // uniform condition, so the barriers below stay in uniform control
            // flow.
            return;
        }
        for (uint e = t; e < kBlock; e += kThreads) {
            const uint b = entriesBase() + (blockBase + e) * 3u;
            sW0[e] = scratch[b];
            sW1[e] = scratch[b + 1u];
            sW2[e] = scratch[b + 2u];
        }
        barrier();
        for (uint k = 2u; k <= kBlock; k <<= 1u) {
            for (uint j = k >> 1u; j >= 1u; j >>= 1u) {
                for (uint q = t; q < pairsPerBlock; q += kThreads) {
                    const uint e = 2u * j * (q / j) + (q % j);
                    localCompareExchange(e, j, ((blockBase + e) & k) == 0u);
                }
                barrier();
            }
        }
        for (uint e = t; e < kBlock; e += kThreads) {
            const uint b = entriesBase() + (blockBase + e) * 3u;
            scratch[b] = sW0[e];
            scratch[b + 1u] = sW1[e];
            scratch[b + 2u] = sW2[e];
        }
        return;
    }

    // Mode 2 — fused strided slab: every stride of stage k whose bit position
    // lies in [pLo, pHi], in shared memory.
    const uint k = uint(overflowSortStep.y);
    const uint pLo = uint(overflowSortStep.z);
    const uint pHi = uint(overflowSortStep.w);
    const uint slabElems = 1u << (pHi - pLo + 1u);
    const uint cBase = g * (kBlock / slabElems);

    // NO whole-block-is-sentinel early-out here, even though pLo == 0 makes the
    // slab contiguous and it looks like mode 1's. Mode 1 runs against the
    // PRE-network state, where [liveCount, cap) is provably all sentinel; by the
    // time any mode-2 pass runs, the network's intermediate descending
    // sub-sequences have migrated real records above liveCount, so skipping a
    // high block drops them out of the sort. Measured: with the early-out in
    // place the rotated shots stayed run-variant (3 distinct hashes / 3 runs) —
    // the exact defect this pass exists to close.

    for (uint e = t; e < kBlock; e += kThreads) {
        const uint i =
            expandIndex(cBase + (e / slabElems), e % slabElems, pLo, pHi);
        const uint b = entriesBase() + i * 3u;
        sW0[e] = scratch[b];
        sW1[e] = scratch[b + 1u];
        sW2[e] = scratch[b + 2u];
    }
    barrier();

    // Shared element e sits at slab (e / slabElems), active (e % slabElems),
    // so a partner differing in one active bit is e ^ stride for every
    // stride < slabElems — the pairing never leaves its slab.
    for (uint stride = slabElems >> 1u; stride >= 1u; stride >>= 1u) {
        for (uint q = t; q < pairsPerBlock; q += kThreads) {
            const uint e = 2u * stride * (q / stride) + (q % stride);
            // Every element of a slab shares the same non-active bits, and the
            // direction bit k = 2^s sits above pHi by construction, so the
            // direction is constant across the slab.
            const uint slabBase = expandIndex(cBase + (e / slabElems), 0u, pLo, pHi);
            localCompareExchange(e, stride, (slabBase & k) == 0u);
        }
        barrier();
    }

    for (uint e = t; e < kBlock; e += kThreads) {
        const uint i =
            expandIndex(cBase + (e / slabElems), e % slabElems, pLo, pHi);
        const uint b = entriesBase() + i * 3u;
        scratch[b] = sW0[e];
        scratch[b + 1u] = sW1[e];
        scratch[b + 2u] = sW2[e];
    }
}

// Canonical-order the view-visibility overflow entry list (#2479) — Metal
// twin of c_per_axis_overflow_sort.glsl. See the GLSL for the full contract:
// pass modes (0 sentinel-fill / 1 fused local sort / 2 fused strided slab),
// the (cell, distance, color) = words (0, 2, 1) key, the mandatory
// pre-network fill (the region above the live range holds stale prior-frame
// entries), sentinel substitution (never thread-skip one side of a
// compare-exchange), the strict compare-exchange, and the strided-slab
// addressing that fuses up to kSortBlockBits strides per dispatch. Plain
// (non-atomic) scratch access is safe here: every dispatch runs behind a
// storage barrier on the append / prior network step (the
// c_resolve_per_axis_blit post-barrier contract). The kernel name resolves to
// c_per_axis_overflow_sort (metalFunctionNameForStage keys off the file stem)
// — it MUST be registered in metal_pipeline.cpp's
// threadgroupSizeForFunctionName (256,1,1); it is NOT an image-atomic
// scratch consumer, so it stays OFF functionUsesImageAtomicScratch.
//
// The fusion exists FOR this backend: Metal creates one compute encoder per
// dispatch with a full resource-table flush (~40 us) and memoryBarrier() is a
// no-op (encoder boundaries serialize), so the unfused network's dispatch
// count — not its arithmetic — was the whole measured cost.
#include "ir_iso_common.metal"

constant uint kSortSentinelWord = 0xFFFFFFFFu;
constant uint kSortThreads = 256u;
// Elements per fused slab: 3 threadgroup words per element = 24 KB, the
// largest power of two under the 32 KB threadgroup floor. MUST match the
// GLSL kBlockBits and the kBlockBits literal in system_voxel_to_trixel.hpp.
constant uint kSortBlockBits = 11u;
constant uint kSortBlock = 1u << kSortBlockBits;

// Lexicographic (cell, distance, color) — words (0, 2, 1).
inline bool overflowRecordLess(
    uint a0, uint a1, uint a2, uint b0, uint b1, uint b2
) {
    if (a0 != b0) return a0 < b0;
    if (a2 != b2) return a2 < b2;
    return a1 < b1;
}

// Reinsert the active-bit window into a compressed slab index.
inline uint overflowExpandIndex(uint c, uint active, uint pLo, uint pHi) {
    return ((c >> pLo) << (pHi + 1u)) | (active << pLo) | (c & ((1u << pLo) - 1u));
}

// Compare-exchange the threadgroup records at e and e+stride toward @p
// ascending order. Strict on both sides: equal records never swap, so a
// sentinel-saturated slab performs zero threadgroup writes.
inline void overflowCompareExchange(
    threadgroup uint *sW0,
    threadgroup uint *sW1,
    threadgroup uint *sW2,
    uint e,
    uint stride,
    bool ascending
) {
    const uint p = e + stride;
    const bool swapNeeded = ascending
        ? overflowRecordLess(sW0[p], sW1[p], sW2[p], sW0[e], sW1[e], sW2[e])
        : overflowRecordLess(sW0[e], sW1[e], sW2[e], sW0[p], sW1[p], sW2[p]);
    if (!swapNeeded) return;
    const uint t0 = sW0[e], t1 = sW1[e], t2 = sW2[e];
    sW0[e] = sW0[p]; sW1[e] = sW1[p]; sW2[e] = sW2[p];
    sW0[p] = t0; sW1[p] = t1; sW2[p] = t2;
}

kernel void c_per_axis_overflow_sort(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    device uint *scratch [[buffer(28)]],
    uint3 globalId [[thread_position_in_grid]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    threadgroup uint sW0[kSortBlock];
    threadgroup uint sW1[kSortBlock];
    threadgroup uint sW2[kSortBlock];

    const uint mode = uint(frameData.overflowSortStep.x);
    const uint entriesBase = uint(frameData.overflowScratchLayout.z);
    const uint capEntries = uint(frameData.overflowScratchLayout.w);
    const uint liveCount = min(
        scratch[uint(frameData.overflowScratchLayout.y) + 1u], capEntries
    );

    if (mode == 0u) {
        // Sentinel fill of [liveCount, cap).
        const uint i = globalId.x;
        if (i >= capEntries || i < liveCount) return;
        const uint b = entriesBase + i * 3u;
        scratch[b] = kSortSentinelWord;
        scratch[b + 1u] = kSortSentinelWord;
        scratch[b + 2u] = kSortSentinelWord;
        return;
    }

    const uint t = localId.x;
    const uint g = groupId.x;
    const uint pairsPerBlock = kSortBlock >> 1u;

    if (mode == 1u) {
        // Full local bitonic over one contiguous kSortBlock-element block:
        // stages k = 2..kSortBlock, all strides. Direction from the GLOBAL
        // element index so these fused stages compose exactly with the
        // strided-slab steps below.
        const uint blockBase = g * kSortBlock;
        if (blockBase >= liveCount) {
            // Whole block is the just-filled sentinel; sorting a constant
            // block is a no-op. Threadgroup-uniform, so the barriers below are
            // reached by all threads or none.
            return;
        }
        for (uint e = t; e < kSortBlock; e += kSortThreads) {
            const uint b = entriesBase + (blockBase + e) * 3u;
            sW0[e] = scratch[b];
            sW1[e] = scratch[b + 1u];
            sW2[e] = scratch[b + 2u];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (uint k = 2u; k <= kSortBlock; k <<= 1u) {
            for (uint j = k >> 1u; j >= 1u; j >>= 1u) {
                for (uint q = t; q < pairsPerBlock; q += kSortThreads) {
                    const uint e = 2u * j * (q / j) + (q % j);
                    overflowCompareExchange(
                        sW0, sW1, sW2, e, j, ((blockBase + e) & k) == 0u
                    );
                }
                threadgroup_barrier(mem_flags::mem_threadgroup);
            }
        }
        for (uint e = t; e < kSortBlock; e += kSortThreads) {
            const uint b = entriesBase + (blockBase + e) * 3u;
            scratch[b] = sW0[e];
            scratch[b + 1u] = sW1[e];
            scratch[b + 2u] = sW2[e];
        }
        return;
    }

    // Mode 2 — fused strided slab: every stride of stage k whose bit position
    // lies in [pLo, pHi], in threadgroup memory.
    const uint k = uint(frameData.overflowSortStep.y);
    const uint pLo = uint(frameData.overflowSortStep.z);
    const uint pHi = uint(frameData.overflowSortStep.w);
    const uint slabElems = 1u << (pHi - pLo + 1u);
    const uint cBase = g * (kSortBlock / slabElems);

    // NO whole-block-is-sentinel early-out here — see the GLSL twin. Mode 1
    // runs against the PRE-network state where [liveCount, cap) is provably all
    // sentinel; by any mode-2 pass the network's intermediate descending
    // sub-sequences have migrated real records above liveCount, so skipping a
    // high block drops them out of the sort (measured: 3 distinct rotated-shot
    // hashes / 3 runs with the early-out in place).

    for (uint e = t; e < kSortBlock; e += kSortThreads) {
        const uint i =
            overflowExpandIndex(cBase + (e / slabElems), e % slabElems, pLo, pHi);
        const uint b = entriesBase + i * 3u;
        sW0[e] = scratch[b];
        sW1[e] = scratch[b + 1u];
        sW2[e] = scratch[b + 2u];
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Threadgroup element e sits at slab (e / slabElems), active
    // (e % slabElems), so a partner differing in one active bit is
    // e ^ stride for every stride < slabElems — the pairing never leaves its
    // slab.
    for (uint stride = slabElems >> 1u; stride >= 1u; stride >>= 1u) {
        for (uint q = t; q < pairsPerBlock; q += kSortThreads) {
            const uint e = 2u * stride * (q / stride) + (q % stride);
            // Every element of a slab shares the same non-active bits, and the
            // direction bit k = 2^s sits above pHi by construction, so the
            // direction is constant across the slab.
            const uint slabBase =
                overflowExpandIndex(cBase + (e / slabElems), 0u, pLo, pHi);
            overflowCompareExchange(
                sW0, sW1, sW2, e, stride, (slabBase & k) == 0u
            );
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    for (uint e = t; e < kSortBlock; e += kSortThreads) {
        const uint i =
            overflowExpandIndex(cBase + (e / slabElems), e % slabElems, pLo, pHi);
        const uint b = entriesBase + i * 3u;
        scratch[b] = sW0[e];
        scratch[b + 1u] = sW1[e];
        scratch[b + 2u] = sW2[e];
    }
}

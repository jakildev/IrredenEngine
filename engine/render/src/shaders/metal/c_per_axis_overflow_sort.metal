// Canonical-order the view-visibility overflow entry list (#2479) — Metal
// twin of c_per_axis_overflow_sort.glsl. See the GLSL for the full contract:
// pass modes (0 sentinel-fill / 1 fused local sort / 2 global bitonic step /
// 3 fused local tail), the (cell, distance, color) = words (0, 2, 1) key,
// the mandatory pre-network fill (the region above the live range holds
// stale prior-frame entries), sentinel substitution (never thread-skip one
// side of a compare-exchange), and the both-sentinel early-out as the only
// sanctioned shortcut. Plain (non-atomic) scratch access is safe here: every
// dispatch runs behind a storage barrier on the append / prior network step
// (the c_resolve_per_axis_blit post-barrier contract). The kernel name
// resolves to c_per_axis_overflow_sort (metalFunctionNameForStage keys off
// the file stem) — it MUST be registered in metal_pipeline.cpp's
// threadgroupSizeForFunctionName (256,1,1); it is NOT an image-atomic
// scratch consumer, so it stays OFF functionUsesImageAtomicScratch.
#include "ir_iso_common.metal"

constant uint kSortSentinelWord = 0xFFFFFFFFu;
// Elements per fused-local block: 256 threads x 2 elements; 3 threadgroup
// words per element = 6 KB.
constant uint kSortBlock = 512u;

// Lexicographic (cell, distance, color) — words (0, 2, 1).
inline bool overflowRecordLess(
    uint a0, uint a1, uint a2, uint b0, uint b1, uint b2
) {
    if (a0 != b0) return a0 < b0;
    if (a2 != b2) return a2 < b2;
    return a1 < b1;
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

    if (mode == 2u) {
        // One global step of stage k at stride j: thread p owns the pair
        // (i, i+j) with i = 2j*(p/j) + (p%j); direction from the global
        // element index's k bit.
        const uint k = uint(frameData.overflowSortStep.y);
        const uint j = uint(frameData.overflowSortStep.z);
        const uint p = globalId.x;
        if (p >= capEntries >> 1u) return;
        const uint i = 2u * j * (p / j) + (p % j);
        const uint bi = entriesBase + i * 3u;
        const uint bp = entriesBase + (i + j) * 3u;
        const uint a0 = scratch[bi];
        const uint b0 = scratch[bp];
        if (a0 == kSortSentinelWord && b0 == kSortSentinelWord) return;
        const uint a1 = scratch[bi + 1u];
        const uint a2 = scratch[bi + 2u];
        const uint b1 = scratch[bp + 1u];
        const uint b2 = scratch[bp + 2u];
        const bool ascending = (i & k) == 0u;
        if (overflowRecordLess(b0, b1, b2, a0, a1, a2) != ascending) return;
        scratch[bi] = b0; scratch[bi + 1u] = b1; scratch[bi + 2u] = b2;
        scratch[bp] = a0; scratch[bp + 1u] = a1; scratch[bp + 2u] = a2;
        return;
    }

    // Modes 1 and 3: fused threadgroup-memory phases over one block.
    const uint blockBase = groupId.x * kSortBlock;
    const uint t = localId.x;

    if (mode == 1u && blockBase >= liveCount) {
        // Pre-network state: the whole block is the just-filled sentinel and
        // sorting a constant block is a no-op. Threadgroup-uniform condition,
        // so the barriers below are reached by all threads or none.
        return;
    }

    for (uint e = t; e < kSortBlock; e += 256u) {
        const uint b = entriesBase + (blockBase + e) * 3u;
        sW0[e] = scratch[b];
        sW1[e] = scratch[b + 1u];
        sW2[e] = scratch[b + 2u];
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    if (mode == 1u) {
        // Full local bitonic: stages k = 2..kSortBlock, all strides. Direction
        // from the GLOBAL element index so these fused stages compose exactly
        // with the global steps.
        for (uint k = 2u; k <= kSortBlock; k <<= 1u) {
            for (uint j = k >> 1u; j >= 1u; j >>= 1u) {
                const uint i = 2u * j * (t / j) + (t % j);
                const uint p = i + j;
                const bool ascending = ((blockBase + i) & k) == 0u;
                if (overflowRecordLess(sW0[p], sW1[p], sW2[p],
                                       sW0[i], sW1[i], sW2[i]) == ascending) {
                    const uint t0 = sW0[i], t1 = sW1[i], t2 = sW2[i];
                    sW0[i] = sW0[p]; sW1[i] = sW1[p]; sW2[i] = sW2[p];
                    sW0[p] = t0; sW1[p] = t1; sW2[p] = t2;
                }
                threadgroup_barrier(mem_flags::mem_threadgroup);
            }
        }
    } else {
        // Mode 3 — local tail of stage k: strides kSortBlock/2 .. 1.
        // k >= 2*kSortBlock and blockBase is kSortBlock-aligned, so the
        // direction bit is constant across the block.
        const uint k = uint(frameData.overflowSortStep.y);
        const bool ascending = (blockBase & k) == 0u;
        for (uint j = kSortBlock >> 1u; j >= 1u; j >>= 1u) {
            const uint i = 2u * j * (t / j) + (t % j);
            const uint p = i + j;
            if (overflowRecordLess(sW0[p], sW1[p], sW2[p],
                                   sW0[i], sW1[i], sW2[i]) == ascending) {
                const uint t0 = sW0[i], t1 = sW1[i], t2 = sW2[i];
                sW0[i] = sW0[p]; sW1[i] = sW1[p]; sW2[i] = sW2[p];
                sW0[p] = t0; sW1[p] = t1; sW2[p] = t2;
            }
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }
    }

    for (uint e = t; e < kSortBlock; e += 256u) {
        const uint b = entriesBase + (blockBase + e) * 3u;
        scratch[b] = sW0[e];
        scratch[b + 1u] = sW1[e];
        scratch[b + 2u] = sW2[e];
    }
}

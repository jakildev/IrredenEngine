#ifndef IR_SUN_DEPTH_SLAB_WRITE_INCLUDED
#define IR_SUN_DEPTH_SLAB_WRITE_INCLUDED

// Primary-layer writer side of the depth-slab extrema in ir_sun_face_query_layout.
// The including kernel declares `sunDepthBuf` as a writable std430 buffer on
// binding 28 and includes ir_sun_face_query_layout.glsl first. Metal twin:
// metal/ir_sun_depth_slab_write.metal.
//
// Every packed primary write lands in the slot owning its slab: the first
// writer of a slab claims the first empty slot with a compare-swap, later
// writers of that slab atomically min / max the pair. A slot's slab never
// changes once claimed, so a plain load that observes a claimed slot is
// final, and a stale EMPTY load is corrected by the swap's returned word.
// A texel with more distinct slabs than slots sets the sticky overflow bit on
// slot 0's max word; the receiver then answers from the nearest word alone.
//
// The pair is one slab's reduced (min, max), both members of the texel's
// write set; a single write is the pair (depth, depth). Reducing writes before
// the merge changes neither the slabs a texel saw nor their extrema.
void mergeSunDepthSlabPair(int texelIndex, uint lowDepth, uint highDepth) {
    if (kSunDepthSlabSlots == 0u) return;
    const uint slab = sunDepthSlab(lowDepth);
    for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
        const uint low = sunDepthSlabMinIndex(texelIndex, slot);
        const uint high = sunDepthSlabMaxIndex(texelIndex, slot);
        uint resident = sunDepthBuf[low];
        if (resident == 0xFFFFFFFFu) {
            resident = atomicCompSwap(sunDepthBuf[low], 0xFFFFFFFFu, lowDepth);
            if (resident == 0xFFFFFFFFu) {
                atomicMax(sunDepthBuf[high], highDepth);
                return;
            }
        }
        if (sunDepthSlab(resident) == slab) {
            // Extrema only move one way, so a plain load that already covers
            // the write proves the atomic would be a no-op; a stale load only
            // issues a redundant one. Coverage splats repeat one depth across
            // a box of texels, so most writes take neither atomic.
            if (lowDepth < resident) atomicMin(sunDepthBuf[low], lowDepth);
            if (highDepth > (sunDepthBuf[high] & ~kSunDepthSlabOverflow)) {
                atomicMax(sunDepthBuf[high], highDepth);
            }
            return;
        }
    }
    const uint flagWord = sunDepthSlabMaxIndex(texelIndex, 0u);
    if ((sunDepthBuf[flagWord] & kSunDepthSlabOverflow) == 0u) {
        atomicOr(sunDepthBuf[flagWord], kSunDepthSlabOverflow);
    }
}

void writeSunDepthSlab(int texelIndex, uint packedDepth) {
    mergeSunDepthSlabPair(texelIndex, packedDepth, packedDepth);
}

// One primary write: the nearest word and its slab extrema together.
void writeSunPrimaryDepth(int texelIndex, uint packedDepth) {
    atomicMin(sunDepthBuf[texelIndex], packedDepth);
    writeSunDepthSlab(texelIndex, packedDepth);
}

#endif

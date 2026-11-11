#ifndef IR_SUN_DEPTH_SLAB_WRITE_INCLUDED
#define IR_SUN_DEPTH_SLAB_WRITE_INCLUDED

// The including kernel supplies the layout and writable slot-28 sunDepthBuf.
// Direct mapping reduces into slab % slots without loading resident words;
// differing min/max slabs tell the receiver to discard the texel's slab layer.
// Associative mapping claims slots and marks overflow on max plane zero.
// A pair contains extrema from one slab, including every provenance bit.
void mergeSunDepthSlabPair(int texelIndex, uint lowDepth, uint highDepth) {
    if (kSunDepthSlabSlots == 0u) return;
    const uint slab = sunDepthSlab(lowDepth);
    if (kSunDepthSlabDirectMapped) {
        const uint slot = slab % (kSunDepthSlabSlots == 0u ? 1u : kSunDepthSlabSlots);
        const uint lowIndex = sunDepthSlabMinIndex(texelIndex, slot);
        const uint highIndex = sunDepthSlabMaxIndex(texelIndex, slot);
        atomicMin(sunDepthBuf[lowIndex], lowDepth);
        atomicMax(sunDepthBuf[highIndex], highDepth);
        return;
    }
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

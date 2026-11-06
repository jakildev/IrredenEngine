#ifndef IR_SUN_DEPTH_SLAB_WRITE_METAL_INCLUDED
#define IR_SUN_DEPTH_SLAB_WRITE_METAL_INCLUDED

#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

// The including kernel supplies the layout and writable slot-28 sunDepthBuf.
// Direct mapping reduces into slab % slots without loading resident words;
// differing min/max slabs tell the receiver to discard the texel's slab layer.
// Associative mapping claims slots and marks overflow on max plane zero.
// A pair contains extrema from one slab, including every provenance bit.
inline void mergeSunDepthSlabPair(
    device atomic_uint *sunDepthBuf, int texelIndex, uint lowDepth, uint highDepth
) {
    if (kSunDepthSlabSlots == 0u) return;
    const uint slab = sunDepthSlab(lowDepth);
    if (kSunDepthSlabDirectMapped) {
        const uint slot = slab % (kSunDepthSlabSlots == 0u ? 1u : kSunDepthSlabSlots);
        const uint lowIndex = sunDepthSlabMinIndex(texelIndex, slot);
        const uint highIndex = sunDepthSlabMaxIndex(texelIndex, slot);
        atomic_fetch_min_explicit(&sunDepthBuf[lowIndex], lowDepth, memory_order_relaxed);
        atomic_fetch_max_explicit(&sunDepthBuf[highIndex], highDepth, memory_order_relaxed);
        return;
    }
    for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
        const uint low = sunDepthSlabMinIndex(texelIndex, slot);
        const uint highIndex = sunDepthSlabMaxIndex(texelIndex, slot);
        uint resident = atomic_load_explicit(&sunDepthBuf[low], memory_order_relaxed);
        if (resident == 0xFFFFFFFFu) {
            // A weak exchange may fail spuriously with the slot still empty.
            while (!atomic_compare_exchange_weak_explicit(
                       &sunDepthBuf[low], &resident, lowDepth,
                       memory_order_relaxed, memory_order_relaxed)) {
                if (resident != 0xFFFFFFFFu) break;
            }
            if (resident == 0xFFFFFFFFu) {
                atomic_fetch_max_explicit(&sunDepthBuf[highIndex], highDepth, memory_order_relaxed);
                return;
            }
        }
        if (sunDepthSlab(resident) == slab) {
            // Extrema only move one way, so a plain load that already covers
            // the write proves the atomic would be a no-op; a stale load only
            // issues a redundant one. Mirrors GLSL.
            if (lowDepth < resident) {
                atomic_fetch_min_explicit(&sunDepthBuf[low], lowDepth, memory_order_relaxed);
            }
            const uint high = atomic_load_explicit(&sunDepthBuf[highIndex], memory_order_relaxed);
            if (highDepth > (high & ~kSunDepthSlabOverflow)) {
                atomic_fetch_max_explicit(&sunDepthBuf[highIndex], highDepth, memory_order_relaxed);
            }
            return;
        }
    }
    const uint flagWord = sunDepthSlabMaxIndex(texelIndex, 0u);
    if ((atomic_load_explicit(&sunDepthBuf[flagWord], memory_order_relaxed) & kSunDepthSlabOverflow) == 0u) {
        atomic_fetch_or_explicit(&sunDepthBuf[flagWord], kSunDepthSlabOverflow, memory_order_relaxed);
    }
}

inline void writeSunDepthSlab(device atomic_uint *sunDepthBuf, int texelIndex, uint packedDepth) {
    mergeSunDepthSlabPair(sunDepthBuf, texelIndex, packedDepth, packedDepth);
}

// One primary write: the nearest word and its slab extrema together. Mirrors GLSL.
inline void writeSunPrimaryDepth(device atomic_uint *sunDepthBuf, int texelIndex, uint packedDepth) {
    atomic_fetch_min_explicit(&sunDepthBuf[texelIndex], packedDepth, memory_order_relaxed);
    writeSunDepthSlab(sunDepthBuf, texelIndex, packedDepth);
}

#endif

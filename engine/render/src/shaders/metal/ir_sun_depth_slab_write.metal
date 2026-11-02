#ifndef IR_SUN_DEPTH_SLAB_WRITE_METAL_INCLUDED
#define IR_SUN_DEPTH_SLAB_WRITE_METAL_INCLUDED

#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

// Mirrors shaders/ir_sun_depth_slab_write.glsl: the primary-layer writer side
// of the depth-slab extrema in ir_sun_face_query_layout.metal, which the
// including kernel includes first. The first writer of a slab claims the
// first empty slot with a compare-exchange; later writers of that slab min /
// max the pair. A slot's slab never changes once claimed. A texel with more
// distinct slabs than slots sets the sticky overflow bit on slot 0's max word.
inline void writeSunDepthSlab(device atomic_uint *sunDepthBuf, int texelIndex, uint packedDepth) {
    if (kSunDepthSlabSlots == 0u) return;
    const uint slab = sunDepthSlab(packedDepth);
    for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
        const uint low = sunDepthSlabMinIndex(texelIndex, slot);
        const uint highIndex = sunDepthSlabMaxIndex(texelIndex, slot);
        uint resident = atomic_load_explicit(&sunDepthBuf[low], memory_order_relaxed);
        if (resident == 0xFFFFFFFFu) {
            // A weak exchange may fail spuriously with the slot still empty.
            while (!atomic_compare_exchange_weak_explicit(
                       &sunDepthBuf[low], &resident, packedDepth,
                       memory_order_relaxed, memory_order_relaxed)) {
                if (resident != 0xFFFFFFFFu) break;
            }
            if (resident == 0xFFFFFFFFu) {
                atomic_fetch_max_explicit(&sunDepthBuf[highIndex], packedDepth, memory_order_relaxed);
                return;
            }
        }
        if (sunDepthSlab(resident) == slab) {
            // Extrema only move one way, so a plain load that already covers
            // the write proves the atomic would be a no-op; a stale load only
            // issues a redundant one. Mirrors GLSL.
            if (packedDepth < resident) {
                atomic_fetch_min_explicit(&sunDepthBuf[low], packedDepth, memory_order_relaxed);
            }
            const uint high = atomic_load_explicit(&sunDepthBuf[highIndex], memory_order_relaxed);
            if (packedDepth > (high & ~kSunDepthSlabOverflow)) {
                atomic_fetch_max_explicit(&sunDepthBuf[highIndex], packedDepth, memory_order_relaxed);
            }
            return;
        }
    }
    const uint flagWord = sunDepthSlabMaxIndex(texelIndex, 0u);
    if ((atomic_load_explicit(&sunDepthBuf[flagWord], memory_order_relaxed) & kSunDepthSlabOverflow) == 0u) {
        atomic_fetch_or_explicit(&sunDepthBuf[flagWord], kSunDepthSlabOverflow, memory_order_relaxed);
    }
}

// One primary write: the nearest word and its slab extrema together. Mirrors GLSL.
inline void writeSunPrimaryDepth(device atomic_uint *sunDepthBuf, int texelIndex, uint packedDepth) {
    atomic_fetch_min_explicit(&sunDepthBuf[texelIndex], packedDepth, memory_order_relaxed);
    writeSunDepthSlab(sunDepthBuf, texelIndex, packedDepth);
}

#endif

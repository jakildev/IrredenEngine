#ifndef IR_SUN_DEPTH_SLAB_WINDOW_METAL_INCLUDED
#define IR_SUN_DEPTH_SLAB_WINDOW_METAL_INCLUDED

#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

// Mirrors shaders/ir_sun_depth_slab_window.glsl: the workgroup-cooperative
// splat of the primary layer over a kSunDepthSlabWindowTexels-square window in
// threadgroup memory. The kernel declares the window arrays and passes them
// down; ir_sun_face_query_layout.metal and ir_sun_depth_slab_write.metal come
// first. The layer's final state is a function of the write set alone, so the
// window only partitions writes between the shared claim and the global write.
constant uint kSunDepthSlabWindowSlotWords =
    (kSunDepthSlabSlots > 0u ? kSunDepthSlabSlots : 1u) * kSunDepthSlabWindowArea;

struct SunDepthSlabWindow {
    threadgroup atomic_uint *nearest;
    threadgroup atomic_uint *slabMin;
    threadgroup atomic_uint *slabMax;
    threadgroup atomic_int *baseMin;
    threadgroup atomic_int *baseMax;
};

inline void sunWindowReset(SunDepthSlabWindow window, uint lane) {
    if (lane == 0u) {
        atomic_store_explicit(&window.baseMin[0], 0x7FFFFFFF, memory_order_relaxed);
        atomic_store_explicit(&window.baseMin[1], 0x7FFFFFFF, memory_order_relaxed);
        atomic_store_explicit(&window.baseMax[0], -0x7FFFFFFF, memory_order_relaxed);
        atomic_store_explicit(&window.baseMax[1], -0x7FFFFFFF, memory_order_relaxed);
    }
}

// A caster lane folds its box's base texel into the footprint reduction.
inline void sunWindowReduceBase(SunDepthSlabWindow window, int2 base) {
    atomic_fetch_min_explicit(&window.baseMin[0], base.x, memory_order_relaxed);
    atomic_fetch_min_explicit(&window.baseMin[1], base.y, memory_order_relaxed);
    atomic_fetch_max_explicit(&window.baseMax[0], base.x, memory_order_relaxed);
    atomic_fetch_max_explicit(&window.baseMax[1], base.y, memory_order_relaxed);
}

inline int2 sunWindowBaseMinimum(SunDepthSlabWindow window) {
    return int2(atomic_load_explicit(&window.baseMin[0], memory_order_relaxed),
                atomic_load_explicit(&window.baseMin[1], memory_order_relaxed));
}

inline int2 sunWindowAnchor(SunDepthSlabWindow window, int radius) {
    return sunWindowBaseMinimum(window) - radius;
}

// Window texels the group's boxes can reach from the anchor, clamped to the
// window; zero when no lane had a caster. Mirrors GLSL.
inline int2 sunWindowExtent(SunDepthSlabWindow window, int radius) {
    const int2 low = sunWindowBaseMinimum(window);
    if (low.x == 0x7FFFFFFF) return int2(0);
    const int2 high = int2(atomic_load_explicit(&window.baseMax[0], memory_order_relaxed),
                           atomic_load_explicit(&window.baseMax[1], memory_order_relaxed));
    return min(high - low + 2 * radius + 1, int2(int(kSunDepthSlabWindowTexels)));
}

// Every lane resets its share of the footprint rectangle to the plane sentinels.
inline void sunWindowInit(SunDepthSlabWindow window, uint lane, uint lanes, int2 extent) {
    const uint cells = uint(extent.x * extent.y);
    for (uint i = lane; i < cells; i += lanes) {
        const uint word = (i / uint(extent.x)) * kSunDepthSlabWindowTexels + i % uint(extent.x);
        atomic_store_explicit(&window.nearest[word], 0xFFFFFFFFu, memory_order_relaxed);
        for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
            atomic_store_explicit(&window.slabMin[slot * kSunDepthSlabWindowArea + word],
                                  0xFFFFFFFFu, memory_order_relaxed);
            atomic_store_explicit(&window.slabMax[slot * kSunDepthSlabWindowArea + word],
                                  0u, memory_order_relaxed);
        }
    }
}

// The slab claim of ir_sun_depth_slab_write over the shared window. Mirrors GLSL.
inline void writeSunDepthSlabWindow(SunDepthSlabWindow window, uint windowIndex, uint packedDepth) {
    const uint slab = sunDepthSlab(packedDepth);
    for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
        const uint word = slot * kSunDepthSlabWindowArea + windowIndex;
        uint resident = atomic_load_explicit(&window.slabMin[word], memory_order_relaxed);
        if (resident == 0xFFFFFFFFu) {
            while (!atomic_compare_exchange_weak_explicit(
                       &window.slabMin[word], &resident, packedDepth,
                       memory_order_relaxed, memory_order_relaxed)) {
                if (resident != 0xFFFFFFFFu) break;
            }
            if (resident == 0xFFFFFFFFu) {
                atomic_fetch_max_explicit(&window.slabMax[word], packedDepth, memory_order_relaxed);
                return;
            }
        }
        if (sunDepthSlab(resident) == slab) {
            if (packedDepth < resident) {
                atomic_fetch_min_explicit(&window.slabMin[word], packedDepth, memory_order_relaxed);
            }
            const uint high = atomic_load_explicit(&window.slabMax[word], memory_order_relaxed);
            if (packedDepth > (high & ~kSunDepthSlabOverflow)) {
                atomic_fetch_max_explicit(&window.slabMax[word], packedDepth, memory_order_relaxed);
            }
            return;
        }
    }
    if ((atomic_load_explicit(&window.slabMax[windowIndex], memory_order_relaxed) &
         kSunDepthSlabOverflow) == 0u) {
        atomic_fetch_or_explicit(&window.slabMax[windowIndex], kSunDepthSlabOverflow,
                                 memory_order_relaxed);
    }
}

// One box write, routed per texel: inside the window the shared claim, outside
// the per-pixel global write behind the cascade bounds guard. Mirrors GLSL.
inline void writeSunTexelCooperative(
    device atomic_uint *sunDepthBuf, SunDepthSlabWindow window,
    int cascadeOffset, int2 px, int2 anchor, uint packedDepth
) {
    const int2 rel = px - anchor;
    if (uint(rel.x) < kSunDepthSlabWindowTexels && uint(rel.y) < kSunDepthSlabWindowTexels) {
        const uint windowIndex = uint(rel.y) * kSunDepthSlabWindowTexels + uint(rel.x);
        atomic_fetch_min_explicit(&window.nearest[windowIndex], packedDepth, memory_order_relaxed);
        writeSunDepthSlabWindow(window, windowIndex, packedDepth);
        return;
    }
    if (px.x < 0 || px.x >= kSunShadowMapDim || px.y < 0 || px.y >= kSunShadowMapDim) {
        return;
    }
    writeSunPrimaryDepth(sunDepthBuf, cascadeOffset + px.y * kSunShadowMapDim + px.x, packedDepth);
}

// Every lane merges its share of the footprint rectangle into the planes. Mirrors GLSL.
inline void sunWindowFlush(
    device atomic_uint *sunDepthBuf, SunDepthSlabWindow window,
    uint lane, uint lanes, int cascadeOffset, int2 anchor, int2 extent
) {
    const uint cells = uint(extent.x * extent.y);
    for (uint i = lane; i < cells; i += lanes) {
        const int2 cell = int2(int(i % uint(extent.x)), int(i / uint(extent.x)));
        const uint word = uint(cell.y) * kSunDepthSlabWindowTexels + uint(cell.x);
        const uint nearest = atomic_load_explicit(&window.nearest[word], memory_order_relaxed);
        if (nearest == 0xFFFFFFFFu) continue;
        const int2 px = anchor + cell;
        if (px.x < 0 || px.x >= kSunShadowMapDim || px.y < 0 || px.y >= kSunShadowMapDim) {
            continue;
        }
        const int index = cascadeOffset + px.y * kSunShadowMapDim + px.x;
        atomic_fetch_min_explicit(&sunDepthBuf[index], nearest, memory_order_relaxed);
        for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
            const uint slotWord = slot * kSunDepthSlabWindowArea + word;
            const uint low = atomic_load_explicit(&window.slabMin[slotWord], memory_order_relaxed);
            if (low == 0xFFFFFFFFu) break;
            const uint high = atomic_load_explicit(&window.slabMax[slotWord], memory_order_relaxed);
            mergeSunDepthSlabPair(sunDepthBuf, index, low, high & ~kSunDepthSlabOverflow);
        }
        if (kSunDepthSlabSlots > 0u &&
            (atomic_load_explicit(&window.slabMax[word], memory_order_relaxed) &
             kSunDepthSlabOverflow) != 0u) {
            atomic_fetch_or_explicit(&sunDepthBuf[sunDepthSlabMaxIndex(index, 0u)],
                                     kSunDepthSlabOverflow, memory_order_relaxed);
        }
    }
}

#endif

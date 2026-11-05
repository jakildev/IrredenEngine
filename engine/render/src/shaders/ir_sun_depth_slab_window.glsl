#ifndef IR_SUN_DEPTH_SLAB_WINDOW_INCLUDED
#define IR_SUN_DEPTH_SLAB_WINDOW_INCLUDED

// Workgroup-cooperative splat of the primary layer: one bake workgroup reduces
// its pixels' splat boxes over a kSunDepthSlabWindowTexels-square window of sun
// texels in threadgroup memory, then merges each window texel once into the
// planes. The including kernel (c_bake_sun_shadow_map_cooperative) declares
// `sunDepthBuf` and includes ir_sun_face_query_layout.glsl and
// ir_sun_depth_slab_write.glsl first. Metal twin: metal/ir_sun_depth_slab_window.metal.
//
// The layer's final state is a function of the write set alone: the nearest
// word is the set's minimum, the slabs are the set's distinct slabs with their
// extrema, and overflow is sticky once a texel saw more slabs than slots. The
// window therefore only partitions writes: a box texel inside the window takes
// the shared claim, one outside takes the global write, and every write lands
// exactly once in a path that ends in the global merge. Window size, anchor
// and group shape move cost, never the result.
//
// Phases, each separated by a workgroup barrier the kernel issues, every lane
// reaching every barrier: reset, reduce the footprint, init, splat, flush. The
// reduction runs first so a tile without a caster has an empty footprint and
// does no window work, and a caster tile clears and flushes only the
// rectangle its boxes can reach.
const uint kSunDepthSlabWindowSlotWords =
    (kSunDepthSlabSlots > 0u ? kSunDepthSlabSlots : 1u) * kSunDepthSlabWindowArea;
shared uint sunWindowNearest[kSunDepthSlabWindowArea];
shared uint sunWindowSlabMin[kSunDepthSlabWindowSlotWords];
shared uint sunWindowSlabMax[kSunDepthSlabWindowSlotWords];
// The workgroup's minimum and maximum base texel (x, y); the window is anchored
// one splat radius below the minimum.
shared int sunWindowBaseMin[2];
shared int sunWindowBaseMax[2];

void sunWindowReset(uint lane) {
    if (lane == 0u) {
        sunWindowBaseMin[0] = 0x7FFFFFFF;
        sunWindowBaseMin[1] = 0x7FFFFFFF;
        sunWindowBaseMax[0] = -0x7FFFFFFF;
        sunWindowBaseMax[1] = -0x7FFFFFFF;
    }
}

// A caster lane folds its box's base texel into the footprint reduction.
void sunWindowReduceBase(ivec2 base) {
    atomicMin(sunWindowBaseMin[0], base.x);
    atomicMin(sunWindowBaseMin[1], base.y);
    atomicMax(sunWindowBaseMax[0], base.x);
    atomicMax(sunWindowBaseMax[1], base.y);
}

ivec2 sunWindowAnchor(int radius) {
    return ivec2(sunWindowBaseMin[0], sunWindowBaseMin[1]) - radius;
}

// Window texels the group's boxes can reach from the anchor, clamped to the
// window; zero when no lane had a caster. A box texel inside the window
// therefore always lies inside this rectangle.
ivec2 sunWindowExtent(int radius) {
    if (sunWindowBaseMin[0] == 0x7FFFFFFF) return ivec2(0);
    const ivec2 span = ivec2(sunWindowBaseMax[0], sunWindowBaseMax[1]) -
                       ivec2(sunWindowBaseMin[0], sunWindowBaseMin[1]) + 2 * radius + 1;
    return min(span, ivec2(int(kSunDepthSlabWindowTexels)));
}

// Every lane resets its share of the footprint rectangle to the plane sentinels.
void sunWindowInit(uint lane, uint lanes, ivec2 extent) {
    const uint cells = uint(extent.x * extent.y);
    for (uint i = lane; i < cells; i += lanes) {
        const uint word = (i / uint(extent.x)) * kSunDepthSlabWindowTexels + i % uint(extent.x);
        sunWindowNearest[word] = 0xFFFFFFFFu;
        for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
            sunWindowSlabMin[slot * kSunDepthSlabWindowArea + word] = 0xFFFFFFFFu;
            sunWindowSlabMax[slot * kSunDepthSlabWindowArea + word] = 0u;
        }
    }
}

// The slab claim of ir_sun_depth_slab_write over the shared window; GLSL cannot
// pass a shared array to a function, so the loop is restated on the window words.
void writeSunDepthSlabWindow(uint windowIndex, uint packedDepth) {
    const uint slab = sunDepthSlab(packedDepth);
    for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
        const uint word = slot * kSunDepthSlabWindowArea + windowIndex;
        uint resident = sunWindowSlabMin[word];
        if (resident == 0xFFFFFFFFu) {
            resident = atomicCompSwap(sunWindowSlabMin[word], 0xFFFFFFFFu, packedDepth);
            if (resident == 0xFFFFFFFFu) {
                atomicMax(sunWindowSlabMax[word], packedDepth);
                return;
            }
        }
        if (sunDepthSlab(resident) == slab) {
            if (packedDepth < resident) atomicMin(sunWindowSlabMin[word], packedDepth);
            if (packedDepth > (sunWindowSlabMax[word] & ~kSunDepthSlabOverflow)) {
                atomicMax(sunWindowSlabMax[word], packedDepth);
            }
            return;
        }
    }
    if ((sunWindowSlabMax[windowIndex] & kSunDepthSlabOverflow) == 0u) {
        atomicOr(sunWindowSlabMax[windowIndex], kSunDepthSlabOverflow);
    }
}

// One box write, routed per texel: inside the window it joins the shared
// reduction, outside it takes the per-pixel global write with the cascade
// bounds guard of writeSunTexel.
void writeSunTexelCooperative(int cascadeOffset, ivec2 px, ivec2 anchor, uint packedDepth) {
    const ivec2 rel = px - anchor;
    if (uint(rel.x) < kSunDepthSlabWindowTexels && uint(rel.y) < kSunDepthSlabWindowTexels) {
        const uint windowIndex = uint(rel.y) * kSunDepthSlabWindowTexels + uint(rel.x);
        atomicMin(sunWindowNearest[windowIndex], packedDepth);
        writeSunDepthSlabWindow(windowIndex, packedDepth);
        return;
    }
    if (px.x < 0 || px.x >= kSunShadowMapDim || px.y < 0 || px.y >= kSunShadowMapDim) {
        return;
    }
    writeSunPrimaryDepth(cascadeOffset + px.y * kSunShadowMapDim + px.x, packedDepth);
}

// Every lane merges its share of the footprint rectangle into the planes: a
// texel nothing wrote is skipped, one outside the cascade is dropped as
// writeSunTexel drops it, otherwise the nearest word, each claimed slot's pair
// and the overflow bit go to the global texel. Slots are claimed in order, so
// the first empty slot ends a texel's list.
void sunWindowFlush(uint lane, uint lanes, int cascadeOffset, ivec2 anchor, ivec2 extent) {
    const uint cells = uint(extent.x * extent.y);
    for (uint i = lane; i < cells; i += lanes) {
        const ivec2 cell = ivec2(int(i % uint(extent.x)), int(i / uint(extent.x)));
        const uint word = uint(cell.y) * kSunDepthSlabWindowTexels + uint(cell.x);
        const uint nearest = sunWindowNearest[word];
        if (nearest == 0xFFFFFFFFu) continue;
        const ivec2 px = anchor + cell;
        if (px.x < 0 || px.x >= kSunShadowMapDim || px.y < 0 || px.y >= kSunShadowMapDim) {
            continue;
        }
        const int index = cascadeOffset + px.y * kSunShadowMapDim + px.x;
        atomicMin(sunDepthBuf[index], nearest);
        for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
            const uint slotWord = slot * kSunDepthSlabWindowArea + word;
            const uint low = sunWindowSlabMin[slotWord];
            if (low == 0xFFFFFFFFu) break;
            mergeSunDepthSlabPair(index, low, sunWindowSlabMax[slotWord] & ~kSunDepthSlabOverflow);
        }
        if (kSunDepthSlabSlots > 0u && (sunWindowSlabMax[word] & kSunDepthSlabOverflow) != 0u) {
            atomicOr(sunDepthBuf[sunDepthSlabMaxIndex(index, 0u)], kSunDepthSlabOverflow);
        }
    }
}

#endif

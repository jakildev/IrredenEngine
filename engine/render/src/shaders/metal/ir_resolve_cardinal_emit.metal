// Shared cardinal-layout micro-cell emit for the sun-shadow RESOLVE scatter
// kernels — Metal twin of ../ir_resolve_cardinal_emit.glsl (keep byte-identical
// math). c_resolve_per_axis_screen_depth and c_resolve_world_placed_depth decode
// differently but end in the same emit; one definition keeps the two resolve
// routes' footprints from drifting against the BAKE recovery that reads them.
//
// Included by the kernel wrappers AFTER ir_iso_common.metal. Unlike the GLSL
// twin — which cannot pass an SSBO as an argument and so writes through a
// wrapper-declared `resolveScratch` binding — Metal passes buffers as function
// arguments, so the scratch pointer is an explicit parameter here.
//
// No `#ifndef ..._INCLUDED` self-guard: the function is `static` (internal
// linkage) and each wrapper includes this file exactly once, so neither in-TU
// re-inclusion nor a standalone glob-compile can raise a duplicate-symbol
// conflict — the ir_voxel_face_select.metal idiom. Do NOT switch it to
// external-linkage `inline`, which is what forces ir_per_axis_lighting.metal
// to carry a guard.

// Prerequisite helpers (pos3DtoDistance, pos3DtoPos2DIso, encodeDepthWithFace,
// faceOffset_2x3, isInsideCanvas). The runtime include resolver is recursive
// with a visited set, so the wrapper's own earlier include of ir_iso_common.metal
// makes this a suppressed duplicate.
#include "ir_iso_common.metal"
#include <metal_atomic>

// Emit one micro-cell's two-pixel diamond region (#1724) into the resolve
// scratch, in the MAIN-canvas cardinal distance layout.
//
// `viewPos` is the micro-cell already rotated into the cardinal VIEW frame and
// expressed in subdivision units; `slot`/`flip` are the stored key bits, which
// ride the encode so polarity survives the resolve bridge (#2207). Emitting the
// two-pixel region rather than the single origin pixel is load-bearing:
// roundHalfUp collapses a region's input pixels onto one recovered cell, so a
// single-pixel write left the resolve texture ~50% sparse — pinhole casters
// whose shadows dithered with interior gaps.
//
// `regionAxis` is the VIEW-frame face axis that picks the diamond region, and
// is NOT always derivable from `slot`: the per-axis store is already in the
// view frame (visibleFaceTripletCardinal orders the triplet so slot s lands on
// view axis s), while the world-placed store is model-frame and must rotate its
// face normal into the view frame first. faceOffset_2x3 is polarity-blind (axis
// only), so the flip never reaches the region choice.
static void emitResolveCardinalDiamond(
    device atomic_int* resolveScratch,
    const int3 viewPos,
    const int regionAxis,
    const int slot,
    const int flip,
    const int2 mainBase,
    const int2 canvasSize
) {
    // Per-micro-cell depth, shared by the region's two pixels — the exact
    // encode a real cardinal store would hold here, so BAKE's pixel+depth
    // inverse recovers points on the face plane.
    const int encoded = encodeDepthWithFace(pos3DtoDistance(viewPos), slot, flip);
    const int2 cellBase = mainBase + pos3DtoPos2DIso(viewPos);
    for (int k = 0; k < 2; ++k) {
        const int2 mainPixel = cellBase + faceOffset_2x3(regionAxis, k);
        if (!isInsideCanvas(mainPixel, canvasSize)) {
            continue;
        }
        // Front-most per screen pixel: smallest encoded distance wins (depth
        // dominates the low bits — flip+slot), exactly like the main canvas
        // atomicMin store.
        const uint idx = uint(mainPixel.y) * uint(canvasSize.x) + uint(mainPixel.x);
        atomic_fetch_min_explicit(&resolveScratch[idx], encoded, memory_order_relaxed);
    }
}

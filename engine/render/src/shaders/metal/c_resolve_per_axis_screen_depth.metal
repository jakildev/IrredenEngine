#include "ir_iso_common.metal"
#include <metal_atomic>
// The cardinal-layout micro-cell emit shared with c_resolve_world_placed_depth.
#include "ir_resolve_cardinal_emit.metal"
// perAxisSubCellFrac — the shared sub-cell frac decode this bridge composes in
// the VIEW frame and the per-axis RECEIVE composes in the world frame (#2816).
#include "ir_per_axis_lighting.metal"

// Mirrors shaders/c_resolve_per_axis_screen_depth.glsl. Re-projects one
// face-local per-axis voxel canvas into a screen-space front-most iso-depth
// scratch buffer laid out exactly like the main canvas distance texture, so
// BAKE_SUN_SHADOW_MAP can cast per-axis voxel shadows through its existing
// cardinal recovery (#1435). Scratch is a buffer (not a texture) because MSL
// has no portable image-atomic syntax — same pattern as
// c_voxel_to_trixel_stage_1.metal's distance scratch.

// Per-axis-only shader; canvas clears to INT_MAX per #1458 encoding.
constant int kEmptyDistanceEncoded = 0x7FFFFFFF;

// #2256: dispatched indirectly over only this axis's OCCUPIED cells (compacted
// by the STAGE_1 per-axis pre-pass). compactedCells holds the occupied linear
// cell indices; cellDrawArgs carries visibleCount at [kDispatchArgsBaseUint + 3].
constant uint kDispatchArgsBaseUint = 8u;      // kPerAxisCellDispatchArgsOffsetBytes / 4
constant uint kPerAxisCellComputeTile = 256u;  // kPerAxisCellComputeTile (16×16 threads)

kernel void c_resolve_per_axis_screen_depth(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    texture2d<int, access::read> perAxisDistances [[texture(0)]],
    device atomic_int* resolveScratch [[buffer(28)]],
    const device uint* compactedCells [[buffer(25)]],
    const device uint* cellDrawArgs [[buffer(26)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint localIndex [[thread_index_in_threadgroup]],
    uint3 numGroups [[threadgroups_per_grid]]
) {
    // Recover the flat list index — the capped 2-D threadgroup grid
    // c_per_axis_cell_finalize wrote (kPerAxisCellComputeTile occupied cells per
    // group) — and decode the cell from the compacted list.
    const uint groupIndex = groupId.x + groupId.y * numGroups.x;
    const uint idx = groupIndex * kPerAxisCellComputeTile + localIndex;
    if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
        return;
    }
    const int2 perAxisSize =
        int2(int(perAxisDistances.get_width()), int(perAxisDistances.get_height()));
    const uint linearCell = compactedCells[idx];
    const int2 cell = int2(int(linearCell) % perAxisSize.x, int(linearCell) / perAxisSize.x);

    const int rawDist = perAxisDistances.read(uint2(cell)).x;
    if (rawDist >= kEmptyDistanceEncoded) {
        return; // occupied per the compaction; guard anyway
    }
    // Per-axis encoding (#1458, flip carrier #2207): rawDepth in world units at
    // bits [31:11]; flip at [10]. The flip is re-emitted into the single-canvas
    // encode below so polarity survives the resolve bridge. Both polarities
    // share the axis + in-plane sweep, so recovery/footprint are unchanged.
    const int rawDepth = decodeDepthPerAxis(rawDist);
    const int slot = decodeSlot(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int faceId = frameData.visibleFaceIds[slot] ^ flip;
    const int axis = faceId >> 1;

    // Recover the face-plane LATTICE origin (canvas-native units) — exact integer
    // inverse, identical to perAxisCellToWorld3D / peraxis_scatter.metal.
    // Whole-iso base anchor (#1944) — must match the store/recovery anchor; the
    // re-projection `scale` below stays density-scaled (subdivided main layout).
    // The encoding's sub-cell frac rides separately, folded into `viewPos` in
    // the view frame below (#2816) — rounding it in here would quantize it away
    // before the layout that can carry it is reached.
    const int2 perAxisBase =
        trixelOriginOffsetZ1(perAxisSize) + int2(floor(frameData.frameCanvasOffset));
    // Un-yawed iso recovery: the store filed this face at
    // `perAxisBase + pos3DtoPos2DIso(facePos)`; invert via isoPixelToPos3D
    // (exact integer facePos for integer cell + rawDepth).
    const int2 isoPix = cell - perAxisBase;
    const int3 origin = roundHalfUp(isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth)));

    // Re-project into the MAIN-canvas cardinal distance layout, mirroring
    // c_voxel_to_trixel_stage_1.metal's cardinal store, so the BAKE cardinal
    // recovery (trixelCanvasPixelToWorld3D) inverts it exactly and agrees with
    // the per-axis RECEIVE (perAxisCellToWorld3DSubCell — the SUB-CELL form,
    // not the lattice one) up to the destination layout's own quantization;
    // see the sub-cell block below (#2816).
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    const int scale = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    // origin is in world units (#1458); scale up to subdivision units for the
    // main-canvas layout so BAKE's trixelCanvasPixelToWorld3D recovers correctly.
    int3 viewPos = origin;
    if (cardinalIndex != 0) {
        // Plain cardinal rotation — no lower-corner shift (#2545); mirrors
        // the stage-1 cardinal store, and the BAKE recovery
        // (trixelCanvasPixelToWorld3D) dropped its undo symmetrically.
        viewPos = rotateCardinalZ(origin, cardinalIndex);
    }
    viewPos *= scale;  // face-plane origin in subdivision units

    const int2 canvasSize = frameData.canvasSizePixels; // MAIN canvas size
    const int2 mainBase = trixelFrameOffset(
        trixelOriginOffsetZ1(canvasSize),
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );

    // Emit the face's full cardinal-layout footprint (#1724), not just the
    // origin pixel: scale² micro-cells (the faceMicroPositionFixed6 u,v sweep
    // the cardinal store makes), each covering its slot's two-pixel diamond
    // region (faceOffset_2x3). A single-pixel write left the resolve texture
    // ~50% sparse at scale 1 and sparser as effSub grew — pinhole casters
    // whose shadows dithered with interior gaps. `slot` doubles as the
    // view-frame face axis: visibleFaceTripletCardinal orders the triplet so
    // slot s's world face rotates onto view axis s (0 = X column, 1 = Y
    // column, 2 = Z row of the 2x3 diamond).
    float3 eu;
    float3 ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    const int3 stepU = rotateCardinalZ(int3(eu), cardinalIndex);
    const int3 stepV = rotateCardinalZ(int3(ev), cardinalIndex);
    // Out-of-plane view-frame unit step — the third basis the wFrac rides.
    const int3 stepW = rotateCardinalZ(int3(faceOutOfPlaneUnitAxis(axis)), cardinalIndex);

    // Sub-cell displacement (#2816) — mirrors the GLSL twin; see
    // c_resolve_per_axis_screen_depth.glsl for the full rationale. This bridge
    // is an ABSOLUTE-POSITION consumer of the per-axis store (it produces a
    // shadow CASTER position), so it owes the ir_per_axis_lighting obligation
    // the RECEIVE side discharges with perAxisCellToWorld3DSubCell. The frac is
    // quantized to SUBDIVISION units in the FACE-LOCAL frame and only then
    // composed against the already-rotated basis: rounding after the rotation
    // would make the deposit cell depend on the yaw quadrant (rotateCardinalZ
    // negates axes, roundHalfUp is not symmetric about zero) and would break
    // the scale-1 byte-identity, since a frac4 of 0 is exactly -0.5 and a
    // negated axis carries it to +1. Quantizing first maps the whole
    // [-0.5, +7/16] range to 0 at scale 1, so that path is unchanged.
    // Folded into viewPos rather than added per micro-cell: the displacement is
    // constant across the scale² sweep, so the loop body stays exactly the
    // pre-fix expression.
    const int3 subCellSteps = roundHalfUp(perAxisSubCellFrac(rawDist) * float(scale));
    viewPos += stepU * subCellSteps.x + stepV * subCellSteps.y + stepW * subCellSteps.z;

    for (int v = 0; v < scale; ++v) {
        for (int u = 0; u < scale; ++u) {
            const int3 microView = viewPos + stepU * u + stepV * v;
            emitResolveCardinalDiamond(
                resolveScratch, microView, slot, slot, flip, mainBase, canvasSize
            );
        }
    }
}

#version 450 core

// Smooth camera Z-yaw — per-axis sun-shadow resolve, scatter pass (#1435).
//
// Re-projects one face-local per-axis voxel canvas into a SCREEN-SPACE
// front-most iso-depth scratch buffer laid out exactly like the main canvas
// distance texture (cardinal-snapped iso pixel, value = pos3DtoDistance<<3 |
// flip<<2 | slot). Dispatched once per axis canvas; imageAtomicMin across the three
// resolves the front-most surface per screen pixel — the same per-screen-pixel
// flattening the main (SDF/text) canvas has, which the raw face-local store
// lacks. BAKE_SUN_SHADOW_MAP then reads the blitted texture through its
// EXISTING cardinal recovery (trixelCanvasPixelToWorld3D), so per-axis voxels
// cast sun shadows again without the cross-face self-occlusion that retired the
// face-local bake in #1380. See docs/design/per-axis-sun-shadow-resolve.md.
//
// The scratch target is an SSBO (not an image) because Metal has no portable
// image-atomic syntax — same reason c_voxel_to_trixel_stage_1.metal taps a
// distance scratch buffer. Cardinal byte-identity is structural: the per-axis
// canvases are only allocated at non-zero residual yaw, so this stage never
// runs at a cardinal.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
// perAxisSubCellFrac — the shared sub-cell frac decode this bridge composes in
// the VIEW frame and the per-axis RECEIVE composes in the world frame (#2816).
#include "ir_per_axis_lighting.glsl"

// Per-axis-only shader; canvas clears to INT_MAX per #1458 encoding.
const int kEmptyDistanceEncoded = 0x7FFFFFFF;

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;       // MAIN canvas size (set by the resolve system)
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;
    uniform vec4 _faceDeformPadding[3];
    uniform ivec4 visibleFaceIds;
};

// Input: ONE per-axis voxel canvas (face-local in-plane store, R32I).
layout(r32i, binding = 0) readonly uniform iimage2D perAxisDistances;

// Output scratch: main-canvas-sized, front-most iso-depth via atomicMin.
// Aliases kBufferIndex_SunShadowDepthMap (slot 28) — the whole resolve stage
// runs strictly before BAKE rebinds slot 28 to the sun depth map.
layout(std430, binding = 28) restrict buffer PerAxisResolveScratch {
    int resolveScratch[];
};

// The cardinal-layout micro-cell emit shared with c_resolve_world_placed_depth.
// Included AFTER ir_iso_common.glsl and AFTER the resolveScratch declaration it
// writes through (the fragment's wrapper contract).
#include "ir_resolve_cardinal_emit.glsl"

// #2256: this stage is dispatched indirectly over only this axis's OCCUPIED
// cells (compacted by the STAGE_1 per-axis pre-pass) instead of sweeping the
// full worst-case grid. compactedCells holds the occupied linear cell indices;
// cellDrawArgs carries the visibleCount at [kDispatchArgsBaseUint + 3].
layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};
layout(std430, binding = 26) readonly buffer PerAxisCellIndirect {
    uint cellDrawArgs[];
};
const uint kDispatchArgsBaseUint = 8u;      // kPerAxisCellDispatchArgsOffsetBytes / 4
const uint kPerAxisCellComputeTile = 256u;  // kPerAxisCellComputeTile (16×16 threads)

void main() {
    // Recover the flat list index — the capped 2-D workgroup grid
    // c_per_axis_cell_finalize wrote (kPerAxisCellComputeTile occupied cells per
    // group) — and decode the cell from the compacted list.
    const uint groupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    const uint idx = groupIndex * kPerAxisCellComputeTile + gl_LocalInvocationIndex;
    if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
        return;
    }
    const ivec2 perAxisSize = imageSize(perAxisDistances);
    const uint linearCell = compactedCells[idx];
    const ivec2 cell = ivec2(int(linearCell) % perAxisSize.x, int(linearCell) / perAxisSize.x);

    const int rawDist = imageLoad(perAxisDistances, cell).x;
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
    const int faceId = visibleFaceIds[slot] ^ flip;
    const int axis = faceId >> 1;

    // Recover the face-plane LATTICE origin — the exact iso inverse
    // perAxisCellToWorld3D / v_peraxis_scatter use (no trig, no 2cos(yaw)+1
    // singularity, since the store index is un-yawed). The store filed this face at
    // `perAxisBase + pos3DtoPos2DIso(facePos)` (ir_per_axis_lighting.glsl).
    // Whole-iso base anchor (#1944) — must match the store/recovery anchor.
    // (The re-projection `scale` below stays density-scaled: it maps the recovered
    // base-resolution origin into the SUBDIVIDED main-canvas cardinal layout.)
    // The encoding's sub-cell frac rides separately, folded into `viewPos` in
    // the view frame below (#2816) — rounding it in here would quantize it away
    // before the layout that can carry it is reached.
    const ivec2 perAxisBase = trixelOriginOffsetZ1(perAxisSize) + ivec2(floor(frameCanvasOffset));
    const ivec2 isoPix = cell - perAxisBase;
    const ivec3 origin = roundHalfUp(isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth)));

    // Re-project into the MAIN-canvas cardinal distance layout, mirroring
    // c_voxel_to_trixel_stage_1's cardinal (perAxisRoute==0) store exactly:
    // rotate the world origin into the cardinal VIEW frame, add the same
    // lower-corner shift (scaled to subdivision units), key by un-yawed iso
    // depth, and place at the un-yawed iso pixel. The BAKE recovery
    // (trixelCanvasPixelToWorld3D with this rasterYaw) is the exact inverse, so
    // the recovered world-pos matches the per-axis RECEIVE
    // (perAxisCellToWorld3DSubCell — the SUB-CELL form, not the lattice one)
    // up to the destination layout's own quantization; see the sub-cell block
    // below (#2816).
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    // origin is in world units (#1458); scale up to subdivision units for the
    // main-canvas layout so BAKE's trixelCanvasPixelToWorld3D recovers correctly.
    ivec3 viewPos = origin;
    if (cardinalIndex != 0) {
        // Plain cardinal rotation — no lower-corner shift (#2545); mirrors
        // the stage-1 cardinal store, and the BAKE recovery
        // (trixelCanvasPixelToWorld3D) dropped its undo symmetrically.
        viewPos = rotateCardinalZ(origin, cardinalIndex);
    }
    viewPos *= scale;  // face-plane origin in subdivision units

    const ivec2 mainBase = trixelFrameOffset(
        trixelOriginOffsetZ1(canvasSizePixels), frameCanvasOffset, voxelRenderOptions
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
    vec3 eu;
    vec3 ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    const ivec3 stepU = rotateCardinalZ(ivec3(eu), cardinalIndex);
    const ivec3 stepV = rotateCardinalZ(ivec3(ev), cardinalIndex);
    // Out-of-plane view-frame unit step — the third basis the wFrac rides.
    const ivec3 stepW = rotateCardinalZ(ivec3(faceOutOfPlaneUnitAxis(axis)), cardinalIndex);

    // Sub-cell displacement (#2816). This bridge is an ABSOLUTE-POSITION
    // consumer of the per-axis store — it converts a store cell into the world
    // position BAKE_SUN_SHADOW_MAP deposits as a shadow CASTER — so it owes the
    // ir_per_axis_lighting obligation the RECEIVE side discharges with
    // perAxisCellToWorld3DSubCell. A lattice-only recovery lands the caster up
    // to half a world cell off the surface the receiver samples, so a
    // fractionally-positioned face reads its own cast at the wrong depth.
    //
    // The frac is quantized to SUBDIVISION units in the FACE-LOCAL frame (where
    // every component sits in [-0.5, +7/16] by construction), and only then
    // composed against the already-rotated basis. Two properties follow, and
    // both are load-bearing:
    //
    //  - Cardinal-independence. rotateCardinalZ NEGATES axes at cardinals 1/2/3,
    //    and roundHalfUp is not symmetric about zero, so rounding AFTER the
    //    rotation would make the deposit cell depend on the yaw quadrant — the
    //    same content at the same sub-cell offset would land differently at
    //    each cardinal.
    //  - Byte-identity with the lattice-only emit at scale == 1, structurally.
    //    roundHalfUp maps the whole [-0.5, +7/16] frac range to 0, so the
    //    displacement is exactly zero. (Rounding after the rotation does NOT
    //    have this property: a frac4 of 0 is exactly -0.5, which a negated
    //    axis turns into +0.5 and roundHalfUp carries to +1.)
    //
    // At scale == 1 the destination genuinely cannot represent the frac — the
    // layout has no resolution finer than the world cell — so the cast/receive
    // seam keeps a residual bounded by half a world cell there. That is the
    // layout's limit, stated rather than silently absorbed; see
    // docs/design/per-axis-sun-shadow-resolve.md.
    // Folded into viewPos rather than added per micro-cell: the displacement is
    // constant across the scale² sweep, so the loop body stays exactly the
    // pre-fix expression.
    const ivec3 subCellSteps = roundHalfUp(perAxisSubCellFrac(rawDist) * float(scale));
    viewPos += stepU * subCellSteps.x + stepV * subCellSteps.y + stepW * subCellSteps.z;

    for (int v = 0; v < scale; ++v) {
        for (int u = 0; u < scale; ++u) {
            const ivec3 microView = viewPos + stepU * u + stepV * v;
            // `slot` twice: the per-axis store is already view-frame, so the
            // slot IS the region axis; the world-placed twin must rotate its
            // model face to get one.
            emitResolveCardinalDiamond(
                microView, slot, slot, flip, mainBase, canvasSizePixels
            );
        }
    }
}

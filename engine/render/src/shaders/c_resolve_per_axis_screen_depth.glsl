#version 450 core

// Smooth camera Z-yaw — per-axis sun-shadow resolve, scatter pass (#1435).
//
// Re-projects one face-local per-axis voxel canvas into a SCREEN-SPACE
// front-most iso-depth scratch buffer laid out exactly like the main canvas
// distance texture (cardinal-snapped iso pixel, value = pos3DtoDistance<<2 |
// slot). Dispatched once per axis canvas; imageAtomicMin across the three
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
    // Per-axis encoding (#1458): rawDepth in world units at bits [31:10].
    const int rawDepth = rawDist >> 10;
    const int slot = rawDist & 3;
    const int faceId = visibleFaceIds[slot];
    const int axis = faceId >> 1;

    // Recover the face-plane origin — the exact iso inverse perAxisCellToWorld3D
    // / v_peraxis_scatter use (no trig, no 2cos(yaw)+1 singularity, since the
    // store index is un-yawed). The store filed this face at
    // `perAxisBase + pos3DtoPos2DIso(facePos)` (ir_per_axis_lighting.glsl).
    // Whole-iso base anchor (#1944) — must match the store/recovery anchor.
    // (The re-projection `scale` below stays density-scaled: it maps the recovered
    // base-resolution origin into the SUBDIVIDED main-canvas cardinal layout.)
    const ivec2 perAxisBase = trixelOriginOffsetZ1(perAxisSize) + ivec2(floor(frameCanvasOffset));
    const ivec2 isoPix = cell - perAxisBase;
    const ivec3 origin = ivec3(round(isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth))));

    // Re-project into the MAIN-canvas cardinal distance layout, mirroring
    // c_voxel_to_trixel_stage_1's cardinal (perAxisRoute==0) store exactly:
    // rotate the world origin into the cardinal VIEW frame, add the same
    // lower-corner shift (scaled to subdivision units), key by un-yawed iso
    // depth, and place at the un-yawed iso pixel. The BAKE recovery
    // (trixelCanvasPixelToWorld3D with this rasterYaw) is the exact inverse, so
    // the recovered world-pos matches the per-axis RECEIVE (perAxisCellToWorld3D)
    // by construction — cast and receive agree.
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    // origin is in world units (#1458); scale up to subdivision units for the
    // main-canvas layout so BAKE's trixelCanvasPixelToWorld3D recovers correctly.
    ivec3 viewPos = origin;
    if (cardinalIndex != 0) {
        viewPos = rotateCardinalZ(origin, cardinalIndex);
        viewPos += cardinalLowerCornerShift(cardinalIndex);  // world units
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
    for (int v = 0; v < scale; ++v) {
        for (int u = 0; u < scale; ++u) {
            const ivec3 microView = viewPos + stepU * u + stepV * v;
            // Per-micro-cell depth, shared by the region's two pixels — the
            // exact encode a real cardinal store would hold here, so BAKE's
            // pixel+depth inverse recovers points on the face plane.
            const int encoded = encodeDepthWithFace(pos3DtoDistance(microView), slot);
            const ivec2 cellBase = mainBase + pos3DtoPos2DIso(microView);
            for (int k = 0; k < 2; ++k) {
                const ivec2 mainPixel = cellBase + faceOffset_2x3(slot, k);
                if (!isInsideCanvas(mainPixel, canvasSizePixels)) {
                    continue;
                }
                // Front-most per screen pixel: smallest encoded distance wins
                // (depth dominates the 2 slot bits), exactly like the main
                // canvas atomicMin store.
                atomicMin(
                    resolveScratch[mainPixel.y * canvasSizePixels.x + mainPixel.x],
                    encoded
                );
            }
        }
    }
}

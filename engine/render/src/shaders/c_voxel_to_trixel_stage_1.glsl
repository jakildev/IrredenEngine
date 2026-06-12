/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_1.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 450 core

layout(local_size_x = 2, local_size_y = 3, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Coordinate chain: World 3D -> Iso 2D -> Canvas pixel
//   canvasPixel = trixelCanvasOffsetZ1 + floor(cameraIso) + pos3DtoPos2DIso(world)
// frameCanvasOffset holds floor(cameraIso) from the CPU side.
// Canvas Y increases upward; the trixel-to-framebuffer pass flips V.
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;         // floor(cameraIso)
    uniform ivec2 trixelCanvasOffsetZ1;     // canvas origin for Z-face voxels
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single-canvas raster
    // (byte-identical); 1/2/3 = the X/Y/Z per-axis canvas pass (#1309).
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;         // trixel canvas dimensions
    uniform ivec2 cullIsoMin;               // iso-space cull viewport (matches CPU chunk mask)
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;                // continuous Z-yaw (radians)
    uniform float rasterYaw;                // cardinal-snap multiple of pi/2 nearest visualYaw
    // visualYaw - rasterYaw, in [-pi/4, pi/4]. The pre-T-293 screen-space
    // residual composite (T-058 / T-322) that consumed this value as a
    // post-trixel rotation was retired by T-323; faceDeform[] below now
    // absorbs it during the trixel emit instead.
    uniform float residualYaw;
    // 1.0 for a detached entity canvas, 0.0 for the world canvas. Gates
    // emitDeformedFace max super-sample level (world: 2, detached: 6).
    uniform float isDetachedCanvas;
    // Per-slot deformation matrix packed column-major into vec4: .xy = col0,
    // .zw = col1 of IRMath::faceDeformationMatrix(axis(visibleFaceIds[slot]),
    // residualYaw). **Indexed by visible-triplet SLOT (0/1/2)**, not by axis —
    // at non-zero cardinal the WORLD face whose matrix lives at slot s
    // changes per `visibleFaceIds[s]`. Identity at residualYaw==0 so the
    // cardinal-snap path stays bit-identical pixel-for-pixel against
    // rasterYaw-only master (T-293 + #1278).
    uniform vec4 faceDeform[3];
    // Per-slot world FaceId (0..5 = X_NEG/X_POS/Y_NEG/Y_POS/Z_NEG/Z_POS) —
    // the three camera-visible faces resolved by
    // `IRMath::visibleFaceTripletCardinal` on the CPU (#1278). Slot 0/1/2
    // map to the workgroup-local face slot returned by `localIDToFace_2x3`;
    // `.w` is std140 padding. At cardinal 0 the default {0, 2, 4} = {X_NEG,
    // Y_NEG, Z_NEG} matches the pre-#1278 lower-coordinate semantics.
    uniform ivec4 visibleFaceIds;
    // Model-frame iso depth axis `R⁻¹·(1,1,1)` for the per-voxel occlusion
    // metric (#1462). (1,1,1) for the world canvas / identity entity, so
    // isoDepthAlongAxis collapses to pos3DtoDistance and the GRID / identity
    // raster stays byte-identical; a rotated DETACHED canvas uploads
    // `IRMath::isoDepthAxisModel(rotation)`. `.w` is std140 padding. Appended
    // after visibleFaceIds (offset 144) — the prefix the other binding-7
    // shaders declare is unchanged, so only this stage reads it.
    uniform vec4 voxelDepthAxis;
};

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

// 12 B per voxel — must match C_Voxel layout in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp.
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

// Stage 1 reads `materialFlagBone.flags` (bits 0..5 are face-occlusion bits
// — see VoxelFlags in component_voxel.hpp) to skip emitting iso-visible
// faces that are blocked by a neighbor. `voxels[]` is still bound for
// Phase 2 (#605), where stage 1 will multiply each voxel position by
// bone_matrix[bone_id] before projecting.
layout(std430, binding = 6) readonly buffer ColorBuffer {
    Voxel voxels[];
};

// Face-occlusion bit indices live at `2 + faceId` in `materialFlagBone`'s
// byte 5, mirroring `IRComponents::VoxelFlags::kFaceOccluded*` in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp. The
// exposed-face test (visible-triplet × exposed-mask, #1278) is centralized
// in `faceIsExposed(flagsByte, faceId)` from ir_iso_common.glsl.

layout(std430, binding = 25) readonly buffer CompactedIndices {
    uint compactedVoxelIndices[];
};

layout(std430, binding = 26) readonly buffer IndirectDispatchParams {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
};

layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;

void writeDistanceTap(const ivec2 canvasPixel, const int voxelDistance) {
    if (!isInsideCanvas(canvasPixel, imageSize(triangleCanvasDistances))) return;
    imageAtomicMin(triangleCanvasDistances, canvasPixel, voxelDistance);
}

// Emit a face's 2x3 trixel block through the deformation matrix D.
// Super-samples by D's magnification to fill the face with no forward-
// mapping gaps. World canvas caps at n=2 (Z-yaw residual ≤ π/4 yields
// column lengths ≤ √3, since |col0|² = 3 - 2(c±s) ≤ 3 for X/Y faces);
// detached canvases cap at n=6 for full SO(3).
// At residualYaw==0 on any canvas the identity D collapses n to 1.
void emitDeformedFace(
    const ivec2 base, const mat2 D, const int voxelDistance, const int faceId,
    const bool reVoxelize
) {
    int maxN = isDetachedCanvas > 0.5 ? 6 : 2;
    int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    float inv = 1.0 / float(n);
    // Conservative coverage (#1557 Option B): a re-voxelize canvas bakes the
    // entity rotation into integer CELL positions, so round-to-cell leaves
    // sub-cell gaps between adjacent rotated cells once projected to iso. Dilate
    // each emitted surface face by ±1px along its two in-plane iso axes so the
    // gap pixels fill with the nearest face (imageAtomicMin keeps the occlusion
    // winner; stage 2's depth re-test paints the matching colour). Every other
    // canvas (world, per-axis, detached forward-scatter) emits the exact
    // footprint — byte-identical to master.
    ivec2 su = ivec2(0);
    ivec2 sv = ivec2(0);
    if (reVoxelize) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            vec2 src = vec2(gl_LocalInvocationID.xy) + vec2(float(sx), float(sy)) * inv;
            ivec2 p = base + roundHalfUp(D * src);
            writeDistanceTap(p, voxelDistance);
            if (reVoxelize) {
                writeDistanceTap(p + su, voxelDistance);
                writeDistanceTap(p - su, voxelDistance);
                writeDistanceTap(p + sv, voxelDistance);
                writeDistanceTap(p - sv, voxelDistance);
            }
        }
    }
}

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const vec4 voxelPosition = positions[voxelIndex];

    // `slot` is the per-voxel visible-triplet index (0/1/2) — a workgroup
    // label that maps to a diamond region (right column / left column / top
    // row) and to the per-slot deformation matrix. `faceId` is the WORLD
    // FaceId (0..5) the camera sees at this slot, resolved by the CPU per
    // cardinal via `IRMath::visibleFaceTripletCardinal` (#1278).
    const int slot = localIDToFace_2x3(gl_LocalInvocationID.xy);
    const int faceId = visibleFaceIds[slot];
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // Re-voxelize marker: detached canvases (visibleFaceIds.w != 0, #1557) bake
    // the entity rotation into the CELL positions and raster at cardinal 0.
    const bool reVoxelize = visibleFaceIds.w != 0;

    // Exposed-face gate (#1278): emit only when the world face this slot
    // renders is BOTH camera-visible (the slot-to-faceId resolution above
    // already guarantees this) AND exposed (neighbor cell empty). Interior
    // voxels of a solid cube emit nothing — surface area, not volume —
    // and the depth-tie ambiguity between interior +X and exterior -X
    // copies that produced the pre-#1278 stripe artifact (#1256) cannot
    // arise because the interior copy was never emitted. Bit position
    // matches `IRComponents::VoxelFlags::kFaceOccluded(faceId)`.
    //
    // BYPASSED for re-voxelize (#1570). The GPU scatter (c_revoxelize_detached)
    // rewrites only the cell POSITIONS; the per-voxel exposed mask in `flags_`
    // is computed ONCE at authoring time (IRPrefab::Voxel::recomputeFaceOccupancy)
    // in the UNROTATED model frame and is never recomputed against the rotated
    // cells — P1's per-frame CPU recompute was removed in P2 (#1556) and never
    // moved to the GPU. Gating rotated-frame faces against that unrotated mask
    // systematically drops whole camera-visible faces as the solid spins away
    // from identity. So re-voxelize emits all three visible-triplet cardinal
    // faces (X_NEG/Y_NEG/Z_NEG) for every cell and lets the depth re-test
    // (imageAtomicMin) keep the front-most surface: a convex/centred solid shows
    // exactly its three iso faces with no holes, and interior cells lose the
    // depth tie. (A later optimisation can recompute the mask against the rotated
    // cells on the GPU to cut interior overdraw.)
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;
    if (!reVoxelize && !faceIsExposed(flagsByte, faceId)) return;

    // At cardinalIndex==0 the rotation is the identity; gating it behind a
    // branch keeps the GLSL/MSL compilers from reshuffling instructions or
    // changing depth-tie ordering on the GPU, so yaw=0 stays byte-identical
    // pixel-for-pixel against master.

    // Per-slot deformation matrix — `D` shapes the diamond corner offsets
    // under residualYaw and (for detached canvas) per-face SO(3). At cardinal
    // 0 + residualYaw==0 every slot's D is the identity, so the per-slot
    // path collapses to faceOffset_2x3(slot, subPixel) — bit-identical
    // pixel positions against the pre-T-293 path.
    const mat2 D = mat2(faceDeform[slot].xy, faceDeform[slot].zw);

    // Smooth camera Z-yaw per-axis routing (T2 / #1309 + T3 / #1310;
    // docs/design/per-axis-trixel-canvas-rotation.md). At perAxisRoute==0 this
    // is skipped and the single-canvas path below runs unchanged (byte-
    // identical to master). At perAxisRoute 1/2/3 we are rasterizing the X/Y/Z
    // axis canvas: emit ONLY the visible face on that axis, reposition its
    // center *continuously* with pos3DtoPos2DIsoYawed (replacing the
    // rotateCardinalZ integer snap, so centers swing smoothly between
    // cardinals), and write the shared world-space depth pos3DtoDistance —
    // identical across all three axis canvases so the framebuffer composite
    // can pick the nearest.
    //
    // T3 (#1310, Option-4 forward scatter): store ONE cell per face center
    // (not the emitDeformedFace super-sampled cluster T2 wrote). The cell sits
    // at the voxel's continuously-yawed iso position; `atomicMin` resolves
    // voxel-vs-voxel occlusion per cell (nearest face on this view ray wins),
    // so every non-empty cell is exactly one occlusion-winning face. The
    // framebuffer scatter (system_trixel_to_framebuffer) then forward-projects
    // each non-empty cell as its true deformed face quad — recovering the
    // world origin from (cell - perAxisBase, depth>>2, visualYaw) — with no
    // gather/parity inverse, so the #1256 stripe class cannot occur. The face
    // SHAPE is reconstructed at scatter time, so the per-slot deform D is no
    // longer applied here.
    if (perAxisRoute != 0) {
        const int axis = perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Face-local in-plane store (#1310 fix): index each face by its two
        // in-plane world axes, a dense collision-free lattice, instead of the
        // yawed iso position (which collapsed compressed-axis faces onto one
        // cell -> dropped faces -> cracks, and recovered via the 2cos(yaw)+1
        // inverse that is singular at +/-120 deg). The scatter recovers the
        // origin from the cell exactly (faceOriginFromInPlane) and forward-
        // projects the deformed face quad. The anchor centers the canvas and is
        // computed identically here and at scatter time from the same
        // perAxisBase + canvasSize. See ir_iso_common.glsl.
        const ivec2 perAxisBase =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
        const ivec2 canvasSize = imageSize(triangleCanvasDistances);
        const ivec3 anchor = faceLocalAnchor(perAxisBase, canvasSize);
        const ivec2 cellBase = faceLocalBase(axis, anchor, canvasSize);
        if (voxelRenderOptions.x == 0) {
            const ivec3 worldPos = ivec3(round(voxelPosition.xyz));
            // Store the FACE-PLANE position (POS faces at the high side, NEG at
            // the low side) so the scatter's faceSpanCorner — which no longer
            // re-applies polarity — lands the quad on the correct plane for both
            // polarities, and the stored depth is the face's true iso distance.
            // faceInPlaneCoords ignores the fixed axis, so the cell is identical
            // to worldPos's; only the recovered fixed-axis plane changes.
            const ivec3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
            // No sub-cell offset at base resolution; encode centre fracs (8,8).
            const int voxelDistance =
                encodeDepthWithFaceFrac(pos3DtoDistance(facePos), slot, 8, 8);
            writeDistanceTap(cellBase + faceInPlaneCoords(faceId, facePos), voxelDistance);
            return;
        }
        // #1458: store at BASE (world-unit) resolution regardless of effSub.
        // Only the z=0 invocation writes; higher z-slices return early.
        // The voxel's continuous sub-cell offset is packed into the lower bits
        // of the encoding so scatter can sub-pixel-shift the face quad.
        if (gl_WorkGroupID.z != 0) return;
        const vec3 worldAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const ivec3 worldPos_sub = ivec3(round(worldAligned));
        const ivec3 facePos_sub = faceMicroPositionFixed6(faceId, worldPos_sub, 0, 0, 1);
        const vec3 fracInCell = worldAligned - vec3(worldPos_sub);
        const int voxelDistance =
            encodeDepthWithFaceFrac(pos3DtoDistance(facePos_sub), slot, axis, fracInCell);
        writeDistanceTap(cellBase + faceInPlaneCoords(faceId, facePos_sub), voxelDistance);
        return;
    }

    if (voxelRenderOptions.x == 0) {
        ivec3 voxelPositionInt = ivec3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        // Encode `slot` (not faceId) in depth — keeps the 2-bit field
        // unchanged, and AO/lighting recover the world faceId via
        // visibleFaceIds[slot] from the same UBO.
        // Detached entities raster in model space (camera yaw zeroed), so the
        // occlusion order projects onto the entity-rotated iso axis (#1462).
        // The world canvas keeps the fixed (1,1,1) via pos3DtoDistance, so its
        // depth is byte-identical to master (voxelPositionInt is integer, so
        // the detached path's roundHalfUp(dot(pos,(1,1,1))) would equal x+y+z
        // at identity too — the branch only guards the GRID fast path).
        const int rawDepth = isDetachedCanvas > 0.5
            ? isoDepthAlongAxis(voxelPositionInt, voxelDepthAxis.xyz)
            : pos3DtoDistance(voxelPositionInt);
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot);
        const ivec2 base =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(base, D, voxelDistance, faceId, reVoxelize);
        return;
    }

    const int subdivisions = max(voxelRenderOptions.y, 1);
    int u = int(gl_WorkGroupID.z) / subdivisions;
    int v = int(gl_WorkGroupID.z) % subdivisions;

    const vec3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 voxelPositionFixed = ivec3(round(voxelPositionAligned * float(subdivisions)));
    const ivec2 frameOffsetFixed =
        trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);

    // Six-face micro position — POS faces start at `voxelPositionFixed.<axis>
    // + subdivisions` (high-coordinate side), NEG faces at
    // `voxelPositionFixed.<axis>` (low-coordinate side, matching the 3-face
    // path bit-for-bit at cardinal 0).
    ivec3 microPositionFixed =
        faceMicroPositionFixed6(faceId, voxelPositionFixed, u, v, subdivisions);
    if (cardinalIndex != 0) {
        microPositionFixed = rotateCardinalZ(microPositionFixed, cardinalIndex);
        // Shift is per-world-unit; scale to subdivision units to match
        // `voxelPositionFixed = round(worldPos * subdivisions)`.
        microPositionFixed += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
    }
    // Detached entities project occlusion depth onto the entity-rotated iso
    // axis (#1462); world/GRID keeps the (x+y+z) fixed-(1,1,1) form. Depth is
    // in subdivision units on both branches, so the encode scale is unchanged.
    const int depthBase = isDetachedCanvas > 0.5
        ? isoDepthAlongAxis(microPositionFixed, voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot);
    const ivec2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(base, D, voxelDistance, faceId, reVoxelize);
}

#include "ir_iso_common.metal"
#include "ir_constants.metal"

// Stage 1 of the voxel→trixel pipeline: each surviving voxel writes a depth
// tap into the canvas distance scratch buffer using atomic-min, so stage 2
// can do front-face resolution.  Reads compacted visible voxel indices
// produced by c_voxel_visibility_compact.metal.
//
// MSL has no portable image-atomic syntax across all macOS versions, so we
// use a sibling scratch buffer the same size as the R32I distance texture.
// See engine/render/include/irreden/render/metal/metal_runtime.hpp.

struct IndirectDispatchParamsRO {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
};

inline void writeDistanceTap(
    int2 canvasPixel,
    int voxelDistance,
    device atomic_int* distanceScratch,
    int2 canvasSize
) {
    if (!isInsideCanvas(canvasPixel, canvasSize)) {
        return;
    }
    const uint linearIndex =
        uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    atomic_fetch_min_explicit(
        &distanceScratch[linearIndex],
        voxelDistance,
        memory_order_relaxed
    );
}

// Emit a face's 2x3 trixel block through the deformation matrix D.
// World canvas: maxN=2 (Z-yaw residual ≤ π/4, column lengths ≤ √3).
// Detached canvas: maxN=6 (full SO(3)).
// See c_voxel_to_trixel_stage_1.glsl for the full contract.
inline void emitDeformedFace(
    int2 base,
    float2x2 D,
    int voxelDistance,
    uint2 localId,
    bool isDetached,
    int faceId,
    bool reVoxelize,
    device atomic_int* distanceScratch,
    int2 canvasSize
) {
    const int maxN = isDetached ? 6 : 2;
    const int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    const float inv = 1.0 / float(n);
    // Conservative coverage (#1557 Option B) — mirror of the GLSL twin. A
    // re-voxelize canvas bakes the rotation into integer CELL positions, so
    // round-to-cell leaves sub-cell gaps; dilate each surface face ±1px along its
    // in-plane iso axes so the gaps fill with the nearest face (atomicMin keeps
    // the occlusion winner; stage 2's depth re-test paints the matching colour).
    int2 su = int2(0);
    int2 sv = int2(0);
    if (reVoxelize) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            const float2 src = float2(localId) + float2(float(sx), float(sy)) * inv;
            const int2 p = base + roundHalfUp(D * src);
            writeDistanceTap(p, voxelDistance, distanceScratch, canvasSize);
            if (reVoxelize) {
                writeDistanceTap(p + su, voxelDistance, distanceScratch, canvasSize);
                writeDistanceTap(p - su, voxelDistance, distanceScratch, canvasSize);
                writeDistanceTap(p + sv, voxelDistance, distanceScratch, canvasSize);
                writeDistanceTap(p - sv, voxelDistance, distanceScratch, canvasSize);
            }
        }
    }
}

// 12 B per voxel — must match C_Voxel layout in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp.
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

// Face-occlusion bit indices live at `2 + faceId` in `materialFlagBone`'s
// byte 5, mirroring `IRComponents::VoxelFlags::kFaceOccluded*`. The
// exposed-face test (visible-triplet × exposed-mask, #1278) is centralized
// in `faceIsExposed(flagsByte, faceId)` from ir_iso_common.metal.

kernel void c_voxel_to_trixel_stage_1(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const Voxel* voxels [[buffer(6)]],
    device const uint* compactedVoxelIndices [[buffer(25)]],
    device const IndirectDispatchParamsRO& indirectParams [[buffer(26)]],
    device atomic_int* distanceScratch [[buffer(16)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId3 [[thread_position_in_threadgroup]]
) {
    const uint compactedIdx = groupId.x + groupId.y * indirectParams.numGroupsX;
    if (compactedIdx >= indirectParams.visibleCount) {
        return;
    }

    const uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const float4 voxelPosition = positions[voxelIndex];
    const uint2 localId = localId3.xy;
    // See c_voxel_to_trixel_stage_1.glsl for the slot/faceId contract (#1278).
    const int slot = localIDToFace_2x3(localId);
    const int faceId = frameData.visibleFaceIds[slot];
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);

    const int2 canvasSize = frameData.canvasSizePixels;

    // Re-voxelize marker (frameData.visibleFaceIds.w != 0, #1557) — see GLSL.
    const bool reVoxelize = frameData.visibleFaceIds.w != 0;

    // Exposed-face gate (#1278), BYPASSED for re-voxelize (#1570). The GPU
    // scatter (c_revoxelize_detached) rewrites only cell POSITIONS; the per-voxel
    // exposed mask in `flags_` is computed once at authoring time in the
    // UNROTATED model frame and is never recomputed against the rotated cells
    // (P1's per-frame CPU recompute was removed in P2 / #1556), so gating
    // rotated-frame faces against it drops whole camera-visible faces as the
    // solid spins. Re-voxelize emits all three visible-triplet cardinal faces and
    // lets the depth re-test keep the front-most surface — see the GLSL twin for
    // the full rationale.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;
    if (!reVoxelize && !faceIsExposed(flagsByte, faceId)) return;

    // Per-slot deformation matrix — see stage 1 GLSL for the contract.
    const float2x2 D = float2x2(
        frameData.faceDeform[slot].xy,
        frameData.faceDeform[slot].zw
    );

    // Smooth camera Z-yaw per-axis routing (T2 / #1309 + T3 / #1310) — see
    // c_voxel_to_trixel_stage_1.glsl for the full contract. perAxisRoute==0
    // falls through to the byte-identical single-canvas path below. T3 stores
    // ONE cell per face center (not the emitDeformedFace cluster); atomicMin
    // resolves occlusion per cell and the framebuffer scatter reconstructs the
    // deformed face quad, so D is no longer applied here.
    if (frameData.perAxisRoute != 0) {
        const int axis = frameData.perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Face-local in-plane store (#1310 fix) — see c_voxel_to_trixel_stage_1.glsl
        // and ir_iso_common.metal. Dense collision-free lattice replaces the
        // yawed iso store (cracks on the compressed axis, singular recovery at
        // +/-120 deg); the scatter recovers the origin exactly and reprojects.
        const int2 perAxisBase = trixelFrameOffset(
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions
        );
        const int3 anchor = faceLocalAnchor(perAxisBase, canvasSize);
        const int2 cellBase = faceLocalBase(axis, anchor, canvasSize);
        if (frameData.voxelRenderOptions.x == 0) {
            const int3 worldPos = int3(round(voxelPosition.xyz));
            // Store the FACE-PLANE position (#1310 seam fix) — mirror of
            // c_voxel_to_trixel_stage_1.glsl. faceInPlaneCoords ignores the
            // fixed axis so the cell is unchanged; the depth now recovers the
            // face plane, which faceSpanCorner spans without re-adding polarity.
            const int3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
            const int voxelDistance =
                encodeDepthWithFace(pos3DtoDistance(facePos), slot);
            writeDistanceTap(
                cellBase + faceInPlaneCoords(faceId, facePos), voxelDistance,
                distanceScratch, canvasSize
            );
            return;
        }
        // subPerAxis is the #1431-capped lattice density (voxelRenderOptions.y).
        // The compact pass sized the indirect dispatch Z count from the
        // UNCAPPED effSub, so groupId.z ranges over effSub², not subPerAxis².
        // Skip surplus sub-cell invocations that overflow the capped grid (they
        // would step a full voxel past the cell). Mirrors the GLSL guard.
        const int subPerAxis = max(frameData.voxelRenderOptions.y, 1);
        const int uPerAxis = int(groupId.z) / subPerAxis;
        const int vPerAxis = int(groupId.z) % subPerAxis;
        if (uPerAxis >= subPerAxis) return;
        const float3 worldAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const int3 worldFixed = int3(round(worldAligned * float(subPerAxis)));
        const int3 microWorld =
            faceMicroPositionFixed6(faceId, worldFixed, uPerAxis, vPerAxis, subPerAxis);
        const int voxelDistance =
            encodeDepthWithFace(microWorld.x + microWorld.y + microWorld.z, slot);
        writeDistanceTap(
            cellBase + faceInPlaneCoords(faceId, microWorld), voxelDistance,
            distanceScratch, canvasSize
        );
        return;
    }

    if (frameData.voxelRenderOptions.x == 0) {
        int3 voxelPositionInt = int3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        // Detached entities raster in model space; project occlusion depth
        // onto the entity-rotated iso axis (#1462). World/GRID keeps the fixed
        // (1,1,1) via pos3DtoDistance — byte-identical. See the GLSL twin.
        const int rawDepth = frameData.isDetachedCanvas > 0.5f
            ? isoDepthAlongAxis(voxelPositionInt, frameData.voxelDepthAxis.xyz)
            : pos3DtoDistance(voxelPositionInt);
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot);
        const int2 base =
            trixelFrameOffset(
                frameData.trixelCanvasOffsetZ1,
                frameData.frameCanvasOffset,
                frameData.voxelRenderOptions
            ) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(base, D, voxelDistance, localId, frameData.isDetachedCanvas > 0.5f, faceId, reVoxelize, distanceScratch, canvasSize);
        return;
    }

    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    const int u = int(groupId.z) / subdivisions;
    const int v = int(groupId.z) % subdivisions;

    const float3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const int3 voxelPositionFixed = int3(round(voxelPositionAligned * float(subdivisions)));
    const int2 frameOffsetFixed = trixelFrameOffset(
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );

    int3 microPositionFixed =
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
    const int depthBase = frameData.isDetachedCanvas > 0.5f
        ? isoDepthAlongAxis(microPositionFixed, frameData.voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot);
    const int2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(base, D, voxelDistance, localId, frameData.isDetachedCanvas > 0.5f, faceId, reVoxelize, distanceScratch, canvasSize);
}

#include "ir_iso_common.metal"
#include "ir_constants.metal"

// Stage 2 of the voxel→trixel pipeline: each surviving voxel re-evaluates the
// canvas distance scratch buffer (populated by stage 1) and, on a depth
// match, writes its color/distance/entityId taps into the trixel canvas
// textures.  Reads compacted visible voxel indices produced by
// c_voxel_visibility_compact.metal.
//
// We read the depth via the scratch buffer (atomic_load) rather than the
// R32I texture, mirroring the write path used by stage 1 — see
// engine/render/include/irreden/render/metal/metal_runtime.hpp.

struct IndirectDispatchParamsRO {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
};

inline void writeColorTap(
    int2 canvasPixel,
    int voxelDistance,
    float4 voxelColor,
    uint2 packedEntityId,
    int2 canvasSize,
    device const atomic_int* distanceScratch,
    texture2d<float, access::write> triangleCanvasColors,
    texture2d<int, access::write> triangleCanvasDistances,
    texture2d<uint, access::write> triangleCanvasEntityIds
) {
    if (!isInsideCanvas(canvasPixel, canvasSize)) {
        return;
    }
    const uint linearIndex =
        uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    const int canvasDistance =
        atomic_load_explicit(&distanceScratch[linearIndex], memory_order_relaxed);
    if (voxelDistance != canvasDistance) {
        return;
    }
    const uint2 pixel = uint2(canvasPixel);
    triangleCanvasColors.write(voxelColor, pixel);
    triangleCanvasDistances.write(int4(voxelDistance, 0, 0, 0), pixel);
    triangleCanvasEntityIds.write(uint4(packedEntityId, 0u, 0u), pixel);
}

// Emit a face's 2x3 trixel block through the deformation matrix D.
// Super-sampling gated by isDetached — see c_voxel_to_trixel_stage_1.glsl
// for the full super-sampling contract.
inline void emitDeformedFace(
    int2 base,
    float2x2 D,
    int voxelDistance,
    float4 voxelColor,
    uint2 packedEntityId,
    uint2 localId,
    bool isDetached,
    int faceId,
    bool reVoxelize,
    int2 canvasSize,
    device const atomic_int* distanceScratch,
    texture2d<float, access::write> triangleCanvasColors,
    texture2d<int, access::write> triangleCanvasDistances,
    texture2d<uint, access::write> triangleCanvasEntityIds
) {
    const int maxN = isDetached ? 6 : 1;
    const int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    const float inv = 1.0 / float(n);
    // Conservative coverage (#1557 Option B) — mirror of stage 1's footprint
    // dilation so the colour/entity tap reaches the same gap pixels the distance
    // tap claimed; writeColorTap's depth re-test paints only the occlusion winner.
    int2 su = int2(0);
    int2 sv = int2(0);
    if (reVoxelize) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            const float2 src = float2(localId) + float2(float(sx), float(sy)) * inv;
            const int2 p = base + roundHalfUp(D * src);
            writeColorTap(
                p, voxelDistance, voxelColor, packedEntityId, canvasSize,
                distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                triangleCanvasEntityIds
            );
            if (reVoxelize) {
                writeColorTap(
                    p + su, voxelDistance, voxelColor, packedEntityId, canvasSize,
                    distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                    triangleCanvasEntityIds
                );
                writeColorTap(
                    p - su, voxelDistance, voxelColor, packedEntityId, canvasSize,
                    distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                    triangleCanvasEntityIds
                );
                writeColorTap(
                    p + sv, voxelDistance, voxelColor, packedEntityId, canvasSize,
                    distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                    triangleCanvasEntityIds
                );
                writeColorTap(
                    p - sv, voxelDistance, voxelColor, packedEntityId, canvasSize,
                    distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                    triangleCanvasEntityIds
                );
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

kernel void c_voxel_to_trixel_stage_2(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const Voxel* voxels [[buffer(6)]],
    device const uint2* entityIds [[buffer(13)]],
    device const uint* compactedVoxelIndices [[buffer(25)]],
    device const IndirectDispatchParamsRO& indirectParams [[buffer(26)]],
    device const atomic_int* distanceScratch [[buffer(16)]],
    texture2d<float, access::write> triangleCanvasColors [[texture(0)]],
    texture2d<int, access::write> triangleCanvasDistances [[texture(1)]],
    texture2d<uint, access::write> triangleCanvasEntityIds [[texture(2)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId3 [[thread_position_in_threadgroup]]
) {
    const uint compactedIdx = groupId.x + groupId.y * indirectParams.numGroupsX;
    if (compactedIdx >= indirectParams.visibleCount) {
        return;
    }

    const uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const float4 voxelPosition = positions[voxelIndex];
    float4 voxelColor = unpackColor(voxels[voxelIndex].colorPacked);
    if (voxelColor.a == 0.0) {
        return;
    }
    const uint2 localId = localId3.xy;
    // See c_voxel_to_trixel_stage_1.glsl for the slot/faceId contract (#1278).
    const int slot = localIDToFace_2x3(localId);
    const int faceId = frameData.visibleFaceIds[slot];

    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    const int2 canvasSize = frameData.canvasSizePixels;
    const uint2 packedEntityId = entityIds[voxelIndex];

    // Re-voxelize marker — mirror of stage 1.
    const bool reVoxelize = frameData.visibleFaceIds.w != 0;

    // Stage 2 mirrors stage 1's exposed-face gate (#1278) so it doesn't waste a
    // depth compare on faces stage 1 skipped — and is BYPASSED for re-voxelize
    // (#1570) for the same reason (its `flags_` mask is in the unrotated
    // authoring frame, not the baked-in rotated cell frame; see the GLSL twin).
    // Stage 1 emitted all three cardinal faces for re-voxelize, so stage 2 must
    // paint them too or the colour tap is lost. writeColorTap's depth re-test
    // still keeps only the occlusion winner among the emitted faces.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;
    if (!reVoxelize && !faceIsExposed(flagsByte, faceId)) return;

    // Per-slot deformation matrix — see stage 1 GLSL for the contract.
    const float2x2 D = float2x2(
        frameData.faceDeform[slot].xy,
        frameData.faceDeform[slot].zw
    );

    // Smooth camera Z-yaw per-axis routing (T2 / #1309 + T3 / #1310) — mirrors
    // stage 1's geometry exactly so the color/entity-id tap lands on the same
    // single center cell. T3 stores one cell per face center; the framebuffer
    // scatter reconstructs the deformed face quad. See
    // c_voxel_to_trixel_stage_1.glsl for the contract.
    if (frameData.perAxisRoute != 0) {
        const int axis = frameData.perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Un-yawed (cardinal) iso store (prototype) — mirrors stage 1's re-key.
        // Color tap lands on the same cardinal iso cell + depth the distance tap
        // did, so the per-axis depth re-test paints the occlusion winner.
        const int2 perAxisBase = trixelFrameOffset(
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions
        );
        if (frameData.voxelRenderOptions.x == 0) {
            const int3 worldPos = int3(round(voxelPosition.xyz));
            const int3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
            // No sub-cell offset at base resolution; encode centre fracs (8,8).
            const int voxelDistance =
                encodeDepthWithFaceFrac(pos3DtoDistance(facePos), slot, 8, 8);
            writeColorTap(
                perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance, voxelColor,
                packedEntityId, canvasSize, distanceScratch,
                triangleCanvasColors, triangleCanvasDistances, triangleCanvasEntityIds
            );
            return;
        }
        // #1458: mirror stage 1's base-resolution store (z=0 only).
        if (groupId.z != 0) return;
        const float3 worldAligned_s2 = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const int3 worldPos_s2 = int3(round(worldAligned_s2));
        const int3 facePos_s2 = faceMicroPositionFixed6(faceId, worldPos_s2, 0, 0, 1);
        const float3 fracInCell_s2 = worldAligned_s2 - float3(worldPos_s2);
        const int voxelDistance_s2 =
            encodeDepthWithFaceFrac(pos3DtoDistance(facePos_s2), slot, axis, fracInCell_s2);
        writeColorTap(
            perAxisBase + pos3DtoPos2DIso(facePos_s2), voxelDistance_s2, voxelColor,
            packedEntityId, canvasSize, distanceScratch,
            triangleCanvasColors, triangleCanvasDistances, triangleCanvasEntityIds
        );
        return;
    }

    // Depth-only shadow-feeder path (#1740) — mirror of c_voxel_to_trixel_stage_2.glsl.
    // On the cardinal single-canvas world route, a voxel whose cardinal iso
    // position lies outside the un-widened visible viewport but inside the
    // shadow-feeder-widened cull is an off-screen SHADOW FEEDER: it only casts
    // sun shadows via stage 1's distance bake and is never displayed/lit/picked.
    // Stage 1 wrote its full-res depth (the bake + AO read only trixelDistances),
    // so skipping its colour/entity-id taps is byte-identical and removes the
    // feeder's stage-2 cost. visibleIsoBounds carries a +4-iso-px margin covering
    // the face footprint; with sun shadows off it equals cullIsoMin/Max so
    // nothing is skipped.
    if (frameData.residualYaw == 0.0f && frameData.isDetachedCanvas < 0.5f) {
        int3 feederPos = int3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            feederPos = rotateCardinalZ(feederPos, cardinalIndex);
            feederPos += cardinalLowerCornerShift(cardinalIndex);
        }
        const int2 feederIso = pos3DtoPos2DIso(feederPos);
        if (feederIso.x < frameData.visibleIsoBounds.x ||
            feederIso.x > frameData.visibleIsoBounds.z ||
            feederIso.y < frameData.visibleIsoBounds.y ||
            feederIso.y > frameData.visibleIsoBounds.w) {
            return;
        }
    }

    if (frameData.voxelRenderOptions.x == 0) {
        int3 voxelPositionInt = int3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        // Detached entities project occlusion depth onto the entity-rotated
        // iso axis (#1462); world/GRID keeps the fixed (1,1,1) via
        // pos3DtoDistance. MUST mirror stage 1's distance-tap depth or the
        // re-test in writeColorTap rejects the tap (#1499).
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
        emitDeformedFace(
            base,
            D,
            voxelDistance,
            voxelColor,
            packedEntityId,
            localId,
            frameData.isDetachedCanvas > 0.5f,
            faceId,
            reVoxelize,
            canvasSize,
            distanceScratch,
            triangleCanvasColors,
            triangleCanvasDistances,
            triangleCanvasEntityIds
        );
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
        microPositionFixed += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
    }
    // Detached: mirror stage 1's entity-rotated occlusion axis (#1462 / #1499);
    // depth is in subdivision units on both branches so the encode is unchanged.
    const int depthBase = frameData.isDetachedCanvas > 0.5f
        ? isoDepthAlongAxis(microPositionFixed, frameData.voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot);
    const int2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(
        base,
        D,
        voxelDistance,
        voxelColor,
        packedEntityId,
        localId,
        frameData.isDetachedCanvas > 0.5f,
        faceId,
        reVoxelize,
        canvasSize,
        distanceScratch,
        triangleCanvasColors,
        triangleCanvasDistances,
        triangleCanvasEntityIds
    );
}

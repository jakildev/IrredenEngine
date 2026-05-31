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
// `maxN` is resolved per voxel by the caller (detached canvas and
// MAIN_CANVAS_SO3 voxels cap at n=6) — see c_voxel_to_trixel_stage_1.glsl for
// the full super-sampling contract.
inline void emitDeformedFace(
    int2 base,
    float2x2 D,
    int voxelDistance,
    float4 voxelColor,
    uint2 packedEntityId,
    uint2 localId,
    int maxN,
    int2 canvasSize,
    device const atomic_int* distanceScratch,
    texture2d<float, access::write> triangleCanvasColors,
    texture2d<int, access::write> triangleCanvasDistances,
    texture2d<uint, access::write> triangleCanvasEntityIds
) {
    const int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    const float inv = 1.0 / float(n);
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            const float2 src = float2(localId) + float2(float(sx), float(sy)) * inv;
            writeColorTap(
                base + roundHalfUp(D * src),
                voxelDistance,
                voxelColor,
                packedEntityId,
                canvasSize,
                distanceScratch,
                triangleCanvasColors,
                triangleCanvasDistances,
                triangleCanvasEntityIds
            );
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
    // Per-entity SO(3) (#1299) — mirror of c_voxel_to_trixel_stage_2.glsl.
    const uint reserved = voxels[voxelIndex].reserved;
    const bool voxelSO3 = frameData.visibleFaceIds[3] != 0 && reservedHasSO3(reserved);
    const int faceId = voxelSO3 ? unpackReservedFaceId(reserved, slot)
                                : frameData.visibleFaceIds[slot];

    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    const int2 canvasSize = frameData.canvasSizePixels;
    const uint2 packedEntityId = entityIds[voxelIndex];

    // Stage 2 mirrors stage 1's exposed-face gate (#1278).
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;
    if (!faceIsExposed(flagsByte, faceId)) return;

    // Per-slot deformation matrix — see stage 1 for the contract. A
    // MAIN_CANVAS_SO3 voxel (#1300, PR-B) reads the canvas's single SO(3)
    // entity's residual face-deform from the per-canvas faceDeformSO3[] UBO field
    // and super-samples at n≤6 so the color tap covers every pixel stage 1 wrote
    // distance to; otherwise the shared per-canvas deform with the existing world
    // cap (n≤1).
    float2x2 D;
    int emitMaxN;
    if (voxelSO3) {
        D = float2x2(frameData.faceDeformSO3[slot].xy, frameData.faceDeformSO3[slot].zw);
        emitMaxN = 6;
    } else {
        D = float2x2(frameData.faceDeform[slot].xy, frameData.faceDeform[slot].zw);
        emitMaxN = frameData.isDetachedCanvas > 0.5f ? 6 : 1;
    }

    // Smooth camera Z-yaw per-axis routing (T2 / #1309 + T3 / #1310) — mirrors
    // stage 1's geometry exactly so the color/entity-id tap lands on the same
    // single center cell. T3 stores one cell per face center; the framebuffer
    // scatter reconstructs the deformed face quad. See
    // c_voxel_to_trixel_stage_1.glsl for the contract.
    if (frameData.perAxisRoute != 0) {
        const int axis = frameData.perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Face-local in-plane store — mirrors stage 1 (#1310 fix). See
        // c_voxel_to_trixel_stage_1.glsl / ir_iso_common.metal.
        const int2 perAxisBase = trixelFrameOffset(
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions
        );
        const int3 anchor = faceLocalAnchor(perAxisBase, canvasSize);
        const int2 cellBase = faceLocalBase(axis, anchor, canvasSize);
        if (frameData.voxelRenderOptions.x == 0) {
            const int3 worldPos = int3(round(voxelPosition.xyz));
            // Mirror stage 1's face-plane store (#1310 seam fix) so the color
            // tap lands on the same cell + depth the distance tap did.
            const int3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
            const int voxelDistance =
                encodeDepthWithFace(pos3DtoDistance(facePos), slot);
            writeColorTap(
                cellBase + faceInPlaneCoords(faceId, facePos), voxelDistance, voxelColor,
                packedEntityId, canvasSize, distanceScratch,
                triangleCanvasColors, triangleCanvasDistances, triangleCanvasEntityIds
            );
            return;
        }
        const int subPerAxis = max(frameData.voxelRenderOptions.y, 1);
        const int uPerAxis = int(groupId.z) / subPerAxis;
        const int vPerAxis = int(groupId.z) % subPerAxis;
        const float3 worldAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const int3 worldFixed = int3(round(worldAligned * float(subPerAxis)));
        const int3 microWorld =
            faceMicroPositionFixed6(faceId, worldFixed, uPerAxis, vPerAxis, subPerAxis);
        const int voxelDistance =
            encodeDepthWithFace(microWorld.x + microWorld.y + microWorld.z, slot);
        writeColorTap(
            cellBase + faceInPlaneCoords(faceId, microWorld), voxelDistance, voxelColor,
            packedEntityId, canvasSize, distanceScratch,
            triangleCanvasColors, triangleCanvasDistances, triangleCanvasEntityIds
        );
        return;
    }

    if (frameData.voxelRenderOptions.x == 0) {
        int3 voxelPositionInt = int3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        const int voxelDistance = encodeDepthWithFace(
            pos3DtoDistance(voxelPositionInt), slot
        );
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
    const int depthBase =
        microPositionFixed.x + microPositionFixed.y + microPositionFixed.z;
    const int voxelDistance = encodeDepthWithFace(depthBase, slot);
    const int2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(
        base,
        D,
        voxelDistance,
        voxelColor,
        packedEntityId,
        localId,
        emitMaxN,
        canvasSize,
        distanceScratch,
        triangleCanvasColors,
        triangleCanvasDistances,
        triangleCanvasEntityIds
    );
}

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
    int2 canvasSize,
    device const atomic_int* distanceScratch,
    texture2d<float, access::write> triangleCanvasColors,
    texture2d<int, access::write> triangleCanvasDistances,
    texture2d<uint, access::write> triangleCanvasEntityIds
) {
    const int maxN = isDetached ? 6 : 1;
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
    const int face = localIDToFace_2x3(localId);

    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    const int2 canvasSize = frameData.canvasSizePixels;
    const uint2 packedEntityId = entityIds[voxelIndex];

    // mat2 D = faceDeformationMatrix(face, residualYaw) — or the SO(3) form
    // for a detached canvas. Identity at residualYaw==0 — see
    // c_voxel_to_trixel_stage_1.metal for the contract. emitDeformedFace
    // super-samples the source block when D magnifies.
    const float2x2 D = float2x2(
        frameData.faceDeform[face].xy,
        frameData.faceDeform[face].zw
    );

    // At cardinalIndex==0 the rotation is the identity; gating it behind a
    // branch keeps the GLSL/MSL compilers from reshuffling instructions or
    // changing depth-tie ordering on the GPU, so yaw=0 stays byte-identical
    // pixel-for-pixel against master.

    if (frameData.voxelRenderOptions.x == 0) {
        int3 voxelPositionInt = int3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
        }
        const int voxelDistance = encodeDepthWithFace(
            pos3DtoDistance(voxelPositionInt), face
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
        faceMicroPositionFixed(face, voxelPositionFixed, u, v);
    if (cardinalIndex != 0) {
        microPositionFixed = rotateCardinalZ(microPositionFixed, cardinalIndex);
    }
    const int depthBase =
        microPositionFixed.x + microPositionFixed.y + microPositionFixed.z;
    const int voxelDistance = encodeDepthWithFace(depthBase, face);
    const int2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
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
}

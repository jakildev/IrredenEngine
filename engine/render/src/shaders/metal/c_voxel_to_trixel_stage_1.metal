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

kernel void c_voxel_to_trixel_stage_1(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const uint* colors [[buffer(6)]],
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
    const int face = localIDToFace_2x3(localId);

    const int2 canvasSize = frameData.canvasSizePixels;

    if (frameData.voxelRenderOptions.x == 0) {
        const int3 voxelPositionInt = int3(round(voxelPosition.xyz));
        const int voxelDistance = encodeDepthWithFace(
            pos3DtoDistance(voxelPositionInt), face
        );
        const int2 canvasPixel =
            frameData.trixelCanvasOffsetZ1 +
            int2(floor(frameData.frameCanvasOffset)) +
            int2(localId) +
            pos3DtoPos2DIso(voxelPositionInt);
        writeDistanceTap(canvasPixel, voxelDistance, distanceScratch, canvasSize);
        return;
    }

    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    const int u = int(groupId.z) / subdivisions;
    const int v = int(groupId.z) % subdivisions;

    const float3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const int3 voxelPositionFixed = int3(round(voxelPositionAligned * float(subdivisions)));
    const int2 frameOffsetFixed =
        frameData.trixelCanvasOffsetZ1 +
        int2(floor(frameData.frameCanvasOffset * float(subdivisions)));

    const int3 microPositionFixed =
        faceMicroPositionFixed(face, voxelPositionFixed, u, v);
    const int depthBase =
        microPositionFixed.x + microPositionFixed.y + microPositionFixed.z;
    const int voxelDistance = encodeDepthWithFace(depthBase, face);
    const int2 canvasPixel =
        frameOffsetFixed + int2(localId) + pos3DtoPos2DIso(microPositionFixed);
    writeDistanceTap(canvasPixel, voxelDistance, distanceScratch, canvasSize);
}

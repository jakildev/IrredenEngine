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
// Super-sampling gated by isDetached — see c_voxel_to_trixel_stage_1.glsl
// for the full super-sampling contract.
inline void emitDeformedFace(
    int2 base,
    float2x2 D,
    int voxelDistance,
    uint2 localId,
    bool isDetached,
    device atomic_int* distanceScratch,
    int2 canvasSize
) {
    const int maxN = isDetached ? 6 : 1;
    const int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    const float inv = 1.0 / float(n);
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            const float2 src = float2(localId) + float2(float(sx), float(sy)) * inv;
            writeDistanceTap(
                base + roundHalfUp(D * src), voxelDistance, distanceScratch, canvasSize
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

// Face-occlusion bit indices — mirror VoxelFlags::kFaceOccluded{Neg,Pos}{X,Y,Z}
// in engine/prefabs/irreden/voxel/components/component_voxel.hpp.
constant uint kVoxelFlagFaceNegX = 1u << 2;
constant uint kVoxelFlagFaceNegY = 1u << 4;
constant uint kVoxelFlagFaceNegZ = 1u << 6;

kernel void c_voxel_to_trixel_stage_1(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const Voxel* voxels [[buffer(6)]],  // unused in stage 1; bound for Phase 2 bone transform (#605)
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
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);

    const int2 canvasSize = frameData.canvasSizePixels;

    // Face-aware skip: see c_voxel_to_trixel_stage_1.glsl for the matching
    // GLSL block. Only the iso-visible world-axis -X/-Y/-Z faces are
    // checked at cardinalIndex==0; non-zero cardinal rotations fall back
    // to emitting all faces until a follow-up wires the rotation-aware
    // lookup.
    if (cardinalIndex == 0) {
        const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;
        if (face == kXFace && (flagsByte & kVoxelFlagFaceNegX) != 0u) return;
        if (face == kYFace && (flagsByte & kVoxelFlagFaceNegY) != 0u) return;
        if (face == kZFace && (flagsByte & kVoxelFlagFaceNegZ) != 0u) return;
    }

    // At cardinalIndex==0 the rotation is the identity; gating it behind a
    // branch keeps the GLSL/MSL compilers from reshuffling instructions or
    // changing depth-tie ordering on the GPU, so yaw=0 stays byte-identical
    // pixel-for-pixel against master.

    // mat2 D = faceDeformationMatrix(face, residualYaw) — or the SO(3) form
    // for a detached canvas. Identity at residualYaw==0 so emitDeformedFace
    // collapses to a single tap at int2(localId) — bit-identical pixel
    // positions against the pre-T-293 path. Metal mat2 mirror of the GLSL
    // `mat2(faceDeform[face].xy, faceDeform[face].zw)`.
    const float2x2 D = float2x2(
        frameData.faceDeform[face].xy,
        frameData.faceDeform[face].zw
    );

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
        emitDeformedFace(base, D, voxelDistance, localId, frameData.isDetachedCanvas > 0.5f, distanceScratch, canvasSize);
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
    emitDeformedFace(base, D, voxelDistance, localId, frameData.isDetachedCanvas > 0.5f, distanceScratch, canvasSize);
}

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
    uniform int _voxelDispatchPadding;
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
    // emitDeformedFace super-sampling (n > 1) to the detached path only.
    uniform float isDetachedCanvas;
    // Per-face deformation matrix packed column-major into vec4: .xy = col0,
    // .zw = col1 of IRMath::faceDeformationMatrix(face, residualYaw). Indexed
    // by kXFace/kYFace/kZFace. Identity at residualYaw==0 so the cardinal-
    // snap path stays bit-identical pixel-for-pixel against rasterYaw-only
    // master (T-293).
    uniform vec4 faceDeform[3];
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

// Face-occlusion bit indices — mirror VoxelFlags::kFaceOccluded{Neg,Pos}{X,Y,Z}
// in engine/prefabs/irreden/voxel/components/component_voxel.hpp.
const uint kVoxelFlagFaceNegX = 1u << 2;
const uint kVoxelFlagFaceNegY = 1u << 4;
const uint kVoxelFlagFaceNegZ = 1u << 6;

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

// Emit a face's 2x3 trixel block through the deformation matrix D. For
// detached canvases (isDetachedCanvas == 1.0), super-samples by D's
// magnification so SO(3) pitch/roll rotations (T-295) fill the face with no
// forward-mapping gaps. For the world canvas (isDetachedCanvas == 0.0),
// n is capped at 1 — single-tap behavior identical to T-293's baseline.
// At residualYaw==0 on any canvas the identity D also collapses n to 1.
void emitDeformedFace(const ivec2 base, const mat2 D, const int voxelDistance) {
    int maxN = isDetachedCanvas > 0.5 ? 6 : 1;
    int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    float inv = 1.0 / float(n);
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            vec2 src = vec2(gl_LocalInvocationID.xy) + vec2(float(sx), float(sy)) * inv;
            writeDistanceTap(base + roundHalfUp(D * src), voxelDistance);
        }
    }
}

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const vec4 voxelPosition = positions[voxelIndex];

    const int face = localIDToFace_2x3(gl_LocalInvocationID.xy);
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // Face-aware skip: at cardinalIndex==0 the iso camera renders the world
    // -X/-Y/-Z faces. If the voxel's neighbor on that face is active, the
    // face is blocked and emitting only wastes an atomicMin. Skipping the
    // tap also keeps the depth texture's tile from a transient max-value
    // overwrite at this pixel. At non-zero cardinalIndex the authored face
    // bits no longer line up with the iso-visible world face direction;
    // skip the optimization in that case until a follow-up wires the
    // rotation-aware lookup (also gates the T-293 C5 deformation work).
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

    // mat2 D = faceDeformationMatrix(face, residualYaw), reconstructed from
    // the std140-packed UBO entry. Identity at residualYaw==0 so the
    // resulting `ivec2 trixelOffset` collapses to faceOffset_2x3(face,
    // subPixel) — bit-identical pixel positions against the pre-T-293 path.
    const mat2 D = mat2(faceDeform[face].xy, faceDeform[face].zw);

    if (voxelRenderOptions.x == 0) {
        ivec3 voxelPositionInt = ivec3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
        }
        const int voxelDistance = encodeDepthWithFace(
            pos3DtoDistance(voxelPositionInt), face);
        const ivec2 base =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(base, D, voxelDistance);
        return;
    }

    const int subdivisions = max(voxelRenderOptions.y, 1);
    int u = int(gl_WorkGroupID.z) / subdivisions;
    int v = int(gl_WorkGroupID.z) % subdivisions;

    const vec3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 voxelPositionFixed = ivec3(round(voxelPositionAligned * float(subdivisions)));
    const ivec2 frameOffsetFixed =
        trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);

    ivec3 microPositionFixed =
        faceMicroPositionFixed(face, voxelPositionFixed, u, v, subdivisions);
    if (cardinalIndex != 0) {
        microPositionFixed = rotateCardinalZ(microPositionFixed, cardinalIndex);
    }
    const int depthBase =
        microPositionFixed.x + microPositionFixed.y + microPositionFixed.z;
    const int voxelDistance = encodeDepthWithFace(depthBase, face);
    const ivec2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(base, D, voxelDistance);
}

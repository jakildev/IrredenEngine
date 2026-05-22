#version 450 core

layout(local_size_x = 2, local_size_y = 3, local_size_z = 1) in;

#include "ir_iso_common.glsl"

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int _voxelDispatchPadding;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;
    // Per-face deformation matrix packed column-major into vec4 (see
    // c_voxel_to_trixel_stage_1.glsl for the layout). T-293.
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

layout(std430, binding = 6) readonly buffer ColorBuffer {
    Voxel voxels[];
};

layout(std430, binding = 13) readonly buffer EntityIdBuffer {
    uvec2 entityIds[];
};

layout(std430, binding = 25) readonly buffer CompactedIndices {
    uint compactedVoxelIndices[];
};

layout(std430, binding = 26) readonly buffer IndirectDispatchParams {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
};

layout(rgba8, binding = 0) writeonly uniform image2D triangleCanvasColors;
layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;
layout(rg32ui, binding = 2) writeonly uniform uimage2D triangleCanvasEntityIds;

void writeColorTap(
    const ivec2 canvasPixel,
    const int voxelDistance,
    const vec4 voxelColor,
    const uint voxelIndex
) {
    if (!isInsideCanvas(canvasPixel, imageSize(triangleCanvasDistances))) return;
    int canvasDistance = imageLoad(triangleCanvasDistances, canvasPixel).x;
    if (voxelDistance == canvasDistance) {
        imageStore(triangleCanvasColors, canvasPixel, voxelColor);
        imageStore(triangleCanvasEntityIds, canvasPixel,
                   uvec4(entityIds[voxelIndex], 0u, 0u));
    }
}

// Emit a face's 2x3 trixel block through the deformation matrix D, super-
// sampling the source block by D's magnification so a stretching deformation
// (detached-canvas pitch/roll, T-295) fills the face with no forward-mapping
// gaps. `n` collapses to 1 whenever both D columns are <= 1 px (identity /
// camera-residual-yaw path), keeping that path byte-identical to master.
void emitDeformedFace(
    const ivec2 base,
    const mat2 D,
    const int voxelDistance,
    const vec4 voxelColor,
    const uint voxelIndex
) {
    int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, 6);
    float inv = 1.0 / float(n);
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            vec2 src = vec2(gl_LocalInvocationID.xy) + vec2(float(sx), float(sy)) * inv;
            writeColorTap(base + roundHalfUp(D * src), voxelDistance, voxelColor, voxelIndex);
        }
    }
}

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const vec4 voxelPosition = positions[voxelIndex];
    vec4 voxelColor = unpackColor(voxels[voxelIndex].colorPacked);
    int face = localIDToFace_2x3(gl_LocalInvocationID.xy);

    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // At cardinalIndex==0 the rotation is the identity; gating it behind a
    // branch keeps the GLSL/MSL compilers from reshuffling instructions or
    // changing depth-tie ordering on the GPU, so yaw=0 stays byte-identical
    // pixel-for-pixel against master.

    // mat2 D = faceDeformationMatrix(face, residualYaw) — or the SO(3) form
    // for a detached canvas. Identity at residualYaw==0 — see
    // c_voxel_to_trixel_stage_1.glsl for the contract. emitDeformedFace
    // super-samples the source block when D magnifies.
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
        emitDeformedFace(base, D, voxelDistance, voxelColor, voxelIndex);
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
    emitDeformedFace(base, D, voxelDistance, voxelColor, voxelIndex);
}

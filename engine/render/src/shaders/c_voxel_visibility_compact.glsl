#version 460 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_constants.glsl"

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
};

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

// 12 B per voxel — must match C_Voxel layout in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp.
struct Voxel {
    uint colorPacked;       // [0:3]  RGBA8
    uint materialFlagBone;  // [4]    material_id | [5] flags | [6] bone_id | [7] pad
    uint reserved;          // [8:11] reserved
};

layout(std430, binding = 6) readonly buffer ColorBuffer {
    Voxel voxels[];
};

layout(std430, binding = 24) readonly buffer ChunkVisibility {
    uint chunkVisible[];
};

layout(std430, binding = 25) writeonly buffer CompactedIndices {
    uint compactedVoxelIndices[];
};

layout(std430, binding = 26) buffer IndirectDispatchParams {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
    uint completedGroups;
};

void main() {
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    uint idx = workGroupIndex * 64u + gl_LocalInvocationID.x;

    if (idx < uint(voxelCount)) {
        uint chunkIdx = idx / uint(VOXEL_CHUNK_SIZE);
        if (chunkVisible[chunkIdx] != 0u) {
            uint packedColor = voxels[idx].colorPacked;
            uint alpha = (packedColor >> 24) & 0xFFu;
            if (alpha != 0u) {
                ivec3 voxelPos = ivec3(round(positions[idx].xyz));
                if (cardinalIndex != 0) {
                    voxelPos = rotateCardinalZ(voxelPos, cardinalIndex);
                }
                ivec2 isoPos = pos3DtoPos2DIso(voxelPos);
                if (isoPos.x >= cullIsoMin.x && isoPos.x <= cullIsoMax.x &&
                    isoPos.y >= cullIsoMin.y && isoPos.y <= cullIsoMax.y) {
                    uint slot = atomicAdd(visibleCount, 1u);
                    compactedVoxelIndices[slot] = idx;
                }
            }
        }
    }

    barrier();
    memoryBarrierBuffer();

    if (gl_LocalInvocationIndex == 0u) {
        uint finished = atomicAdd(completedGroups, 1u) + 1u;
        uint totalGroups = gl_NumWorkGroups.x * gl_NumWorkGroups.y;
        if (finished == totalGroups) {
            uint count = atomicAdd(visibleCount, 0u);
            uint gx = min(count, 1024u);
            numGroupsX = max(gx, 1u);
            numGroupsY = max((count + max(gx, 1u) - 1u) / max(gx, 1u), 1u);
            int subdivisions = max(voxelRenderOptions.y, 1);
            numGroupsZ = (voxelRenderOptions.x != 0) ? uint(subdivisions * subdivisions) : 1u;
        }
    }
}

#version 450 core

// GPU voxel-position prepass (#1396). One thread per live voxel: look up the
// voxel's transform slot (bit-packed into the local-position .w lane), and (for
// GPU-transformed voxels only) compute world = modelToWorld * localPos and write
// the shared global-position SSBO (binding 5) that VOXEL_TO_TRIXEL_STAGE_1 reads.
// Voxels whose slot is the sentinel (kVoxelTransformStatic) are left untouched,
// so the CPU-direct pending-range flush still owns their binding-5 slots
// (byte-identical).
//
// The 2D workgroup grid + linear index reconstruction mirror
// c_voxel_visibility_compact.glsl exactly (same dispatch helper on the CPU).

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

const uint VOXEL_TRANSFORM_STATIC = 0xFFFFFFFFu;

layout(std430, binding = 5) buffer GlobalPositionBuffer {
    vec4 globalPositions[];
};

// .xyz = entity-local position, .w = transform slot bit-cast to float.
layout(std430, binding = 17) readonly buffer LocalPositionBuffer {
    vec4 localPositions[];
};

layout(std430, binding = 18) readonly buffer EntityTransformBuffer {
    mat4 transforms[];
};

layout(std140, binding = 19) uniform UpdateParams {
    int voxelCount;
};

void main() {
    uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    uint voxelId = workGroupIndex * 64u + gl_LocalInvocationID.x;
    if (voxelId >= uint(voxelCount)) {
        return;
    }

    vec4 localEntry = localPositions[voxelId];
    uint slot = floatBitsToUint(localEntry.w);
    if (slot == VOXEL_TRANSFORM_STATIC) {
        return; // CPU-direct voxel — leave binding 5 as the CPU flush wrote it.
    }

    vec3 worldPos = (transforms[slot] * vec4(localEntry.xyz, 1.0)).xyz;
    globalPositions[voxelId] = vec4(worldPos, 1.0);
}

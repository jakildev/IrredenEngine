#include <metal_stdlib>
using namespace metal;

// GPU voxel-position prepass (#1396). Mirrors shaders/c_update_voxel_positions.glsl
// byte-for-byte: one thread per live voxel computes world = modelToWorld * localPos
// for GPU-transformed voxels and writes the shared global-position buffer
// (buffer 5); sentinel-slot (kVoxelTransformStatic) voxels are left untouched so
// the CPU-direct path keeps owning them (byte-identical). The per-voxel transform
// slot is bit-packed into the local-position .w lane. The threadgroup grid +
// linear index reconstruction match c_voxel_visibility_compact.metal exactly.

constant uint VOXEL_TRANSFORM_STATIC = 0xFFFFFFFFu;

struct UpdateParams {
    int voxelCount;
};

kernel void c_update_voxel_positions(
    device float4* globalPositions [[buffer(5)]],
    device const float4* localPositions [[buffer(17)]],
    device const float4x4* transforms [[buffer(18)]],
    constant UpdateParams& params [[buffer(19)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    const uint workGroupIndex = groupId.x + groupId.y * groupCount.x;
    const uint voxelId = workGroupIndex * 64u + localId.x;
    if (voxelId >= uint(params.voxelCount)) {
        return;
    }

    const float4 localEntry = localPositions[voxelId];
    const uint slot = as_type<uint>(localEntry.w);
    if (slot == VOXEL_TRANSFORM_STATIC) {
        return; // CPU-direct voxel — leave buffer 5 as the CPU flush wrote it.
    }

    const float3 worldPos = (transforms[slot] * float4(localEntry.xyz, 1.0)).xyz;
    globalPositions[voxelId] = float4(worldPos, 1.0);
}

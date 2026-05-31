#include <metal_stdlib>
using namespace metal;

// Per-frame voxel transform pass: each thread maps a global voxel index to
// its owning entity, looks up the entity world position, and writes the
// transformed world position into the global position buffer.  Mirrors
// shaders/c_update_voxel_positions.glsl.

struct EntityTransform {
    float4 worldPosition;
    uint poolOffset;
    uint voxelCount;
    uint padding0;
    uint padding1;
};

struct UpdateParams {
    int entityCount;
};

kernel void c_update_voxel_positions(
    device float4* globalPositions [[buffer(5)]],
    device const float4* localPositions [[buffer(17)]],
    device const EntityTransform* transforms [[buffer(18)]],
    constant UpdateParams& params [[buffer(19)]],
    uint globalId [[thread_position_in_grid]]
) {
    uint entityIdx = 0u;
    uint voxelOffset = 0u;
    uint cumulative = 0u;
    bool found = false;
    for (uint e = 0u; e < uint(params.entityCount); ++e) {
        const uint nextCumulative = cumulative + transforms[e].voxelCount;
        if (globalId < nextCumulative) {
            entityIdx = e;
            voxelOffset = globalId - cumulative;
            found = true;
            break;
        }
        cumulative = nextCumulative;
    }

    if (!found) {
        return;
    }

    const uint poolIdx = transforms[entityIdx].poolOffset + voxelOffset;
    const float3 localPos = localPositions[poolIdx].xyz;
    const float3 worldPos = localPos + transforms[entityIdx].worldPosition.xyz;
    globalPositions[poolIdx] = float4(worldPos, 1.0);
}

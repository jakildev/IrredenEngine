#version 460 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct EntityTransform {
    vec4 worldPosition;
    uint poolOffset;
    uint voxelCount;
    uint _padding0;
    uint _padding1;
};

layout(std430, binding = 5) buffer GlobalPositionBuffer {
    vec4 globalPositions[];
};

layout(std430, binding = 17) readonly buffer LocalPositionBuffer {
    vec4 localPositions[];
};

layout(std430, binding = 18) readonly buffer EntityTransformBuffer {
    EntityTransform transforms[];
};

layout(std140, binding = 19) uniform UpdateParams {
    uniform int entityCount;
};

void main() {
    uint globalId = gl_GlobalInvocationID.x;

    uint entityIdx = 0;
    uint voxelOffset = 0;
    uint cumulative = 0;
    bool found = false;
    for (uint e = 0; e < uint(entityCount); ++e) {
        uint nextCumulative = cumulative + transforms[e].voxelCount;
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

    uint poolIdx = transforms[entityIdx].poolOffset + voxelOffset;
    vec3 localPos = localPositions[poolIdx].xyz;
    vec3 worldPos = localPos + transforms[entityIdx].worldPosition.xyz;
    globalPositions[poolIdx] = vec4(worldPos, 1.0);
}
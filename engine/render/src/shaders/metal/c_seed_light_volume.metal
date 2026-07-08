#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_seed_light_volume.glsl. One thread per active
// light source writes its emissive RGB into the volume texel at the
// light's world voxel origin, with alpha = the CPU-computed seed
// residual (`coneAndSeedAlpha.y`): 1.0 for in-window lights,
// distance-discounted for out-of-window lights seeded at the clamped
// window edge (see `gatherLightSources`). The propagate pass decrements
// alpha by `stepFalloff` per step.

struct GPULightSource {
    float4 originAndType;
    float4 colorAndIntensity;
    float4 directionAndRadius;
    float4 coneAndSeedAlpha;
};

struct LightVolumeParams {
    int gridSize;
    int halfExtent;
    int lightCount;
    float stepFalloff;
    // Phase 1c (#360): camera-anchored window. Subtract this world voxel
    // before mapping the light's world origin into a local texel index.
    int4 worldOriginVoxel;
};

kernel void c_seed_light_volume(
    device const GPULightSource *lights [[buffer(4)]],
    constant LightVolumeParams &params [[buffer(23)]],
    texture3d<float, access::write> lightVolume [[texture(0)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const uint lightIndex = globalId.x;
    if (lightIndex >= uint(params.lightCount)) {
        return;
    }

    const GPULightSource light = lights[lightIndex];
    const int3 worldOrigin = int3(light.originAndType.xyz);
    const int3 cell =
        (worldOrigin - params.worldOriginVoxel.xyz) + int3(params.halfExtent);
    if (cell.x < 0 || cell.x >= params.gridSize ||
        cell.y < 0 || cell.y >= params.gridSize ||
        cell.z < 0 || cell.z >= params.gridSize) {
        return;
    }

    const float3 emit = clamp(
        light.colorAndIntensity.rgb * light.colorAndIntensity.a,
        float3(0.0),
        float3(1.0)
    );
    const float seedAlpha = clamp(light.coneAndSeedAlpha.y, 0.0, 1.0);
    lightVolume.write(float4(emit, seedAlpha), uint3(cell));
}

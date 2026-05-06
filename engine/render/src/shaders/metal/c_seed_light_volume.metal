#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_seed_light_volume.glsl. One thread per active
// light source writes its emissive RGB into the volume texel at the
// light's world voxel origin, with alpha=1.0 (full residual strength,
// decremented by `stepFalloff` per step in the propagate pass).

struct GPULightSource {
    float4 originAndType;
    float4 colorAndIntensity;
    float4 directionAndRadius;
    float4 coneAndPad;
};

struct LightVolumeParams {
    int gridSize;
    int halfExtent;
    int lightCount;
    float stepFalloff;
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
    const int3 cell = worldOrigin + int3(params.halfExtent);
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
    lightVolume.write(float4(emit, 1.0), uint3(cell));
}

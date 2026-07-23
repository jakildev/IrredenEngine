#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_seed_light_volume.glsl. One thread per active
// light source writes its emissive RGB into the volume texel at the
// light's world voxel origin, with alpha = the CPU-computed seed
// residual (`coneAndSeedAlpha.y`): 1.0 for in-window lights,
// distance-discounted for out-of-window lights seeded at the clamped
// window edge (see `gatherLightSources`). The propagate pass decrements
// alpha by `stepFalloff` per step.

// GPULightSource layout (the light-list entry this kernel seeds from) and the
// shared LightVolumeParams UBO layout. Phase 1c (#360): subtract
// `worldOriginVoxel.xyz` — the camera-anchored window origin — before mapping a
// light's world origin into a local texel index; `.w` is the has-SPOT flag
// (#2318), unused by the seed.
#include "ir_world_lighting.metal"

kernel void c_seed_light_volume(
    device const GPULightSource *lights [[buffer(4)]],
    constant LightVolumeParams &params [[buffer(23)]],
    texture3d<float, access::write> lightVolume [[texture(0)]],
    // Winning-light ID read texture (#2318): seed writes `lightIndex/255`
    // into `.r` (index+1 so 0 stays "no light").
    texture3d<float, access::write> lightVolumeId [[texture(1)]],
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

    // Winning-light ID: index+1 so 0 stays "no light". RGBA8 `.r` holds only
    // 0–255, so the 256th light (index 255 → id 256) seeds as a plain omni
    // sphere (id 0, no cone) — a non-issue for the "few dozen lights" cap.
    // Skipped when no SPOT was seeded (worldOriginVoxel.w == 0): the cleared id
    // volume stays 0 and the consumer never reads it (byte-identical).
    if (params.worldOriginVoxel.w != 0) {
        const uint idPlusOne = lightIndex + 1u;
        const float idNorm = (idPlusOne <= 255u) ? float(idPlusOne) / 255.0 : 0.0;
        lightVolumeId.write(float4(idNorm, 0.0, 0.0, 0.0), uint3(cell));
    }
}

#version 450 core

// Seeds the 128³ light-volume image with one bright texel per active
// light source. Invoked once per frame after the clear pass and before
// the propagate dilation chain. Each thread owns one entry of the
// `LightSourceBuffer` SSBO (CPU-uploaded by `system_compute_light_volume`),
// converts the light's world voxel origin into a volume texel index
// `(world + halfExtent)`, and writes:
//   - rgb = emissive color × intensity (clamped so the RGBA8 image can
//     hold it).
//   - alpha = the CPU-computed seed residual (`coneAndSeedAlpha.y`): 1.0
//     for lights inside the camera-anchored window, distance-discounted
//     for out-of-window lights seeded at the clamped window edge (the
//     discount reproduces the exact in-window field of the distant light
//     — see `gatherLightSources`). Each propagate step decrements alpha
//     by `stepFalloff`; the consumer reads `rgb * alpha` so light fades
//     linearly with Manhattan distance and stops cleanly at the light's
//     residual budget.
//
// `imageStore` is a plain write — if two lights share an origin texel
// the later thread wins. The propagate pass picks the brightest
// (highest residual alpha) candidate per cell, so the closest light
// dominates overlap regions. Per-channel mixing of overlapping lights
// is deferred to a follow-up pass.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct GPULightSource {
    vec4 originAndType;
    vec4 colorAndIntensity;
    vec4 directionAndRadius;
    vec4 coneAndSeedAlpha;
};

layout(std430, binding = 4) readonly buffer LightSourceBuffer {
    GPULightSource lights[];
};

layout(std140, binding = 23) uniform LightVolumeParams {
    int gridSize;
    int halfExtent;
    int lightCount;
    float stepFalloff;
    // Phase 1c (#360): camera-anchored window. Subtract this world voxel
    // before mapping the light's world origin into a local texel index.
    ivec4 lightVolumeWorldOrigin;
};

layout(rgba8, binding = 0) writeonly uniform image3D lightVolume;
// Winning-light ID channel (#2318): the seed writes `lightIndex + 1`
// (0 = no light) so the propagate pass and the lighting consumer can
// recover which light source lit each cell and apply its SPOT cone.
layout(r16ui, binding = 1) writeonly uniform uimage3D lightVolumeId;

void main() {
    const uint lightIndex = gl_GlobalInvocationID.x;
    if (lightIndex >= uint(lightCount)) {
        return;
    }

    const GPULightSource light = lights[lightIndex];
    const ivec3 worldOrigin = ivec3(light.originAndType.xyz);
    const ivec3 cell = (worldOrigin - lightVolumeWorldOrigin.xyz) + ivec3(halfExtent);
    if (cell.x < 0 || cell.x >= gridSize ||
        cell.y < 0 || cell.y >= gridSize ||
        cell.z < 0 || cell.z >= gridSize) {
        return;
    }

    const vec3 emit = clamp(
        light.colorAndIntensity.rgb * light.colorAndIntensity.a,
        vec3(0.0),
        vec3(1.0)
    );
    const float seedAlpha = clamp(light.coneAndSeedAlpha.y, 0.0, 1.0);
    imageStore(lightVolume, cell, vec4(emit, seedAlpha));
    // `imageStore` is last-writer-wins on a shared origin texel — the same
    // race the color write above has; the propagate pass then keeps the
    // higher-residual candidate, so the closest light's ID dominates.
    imageStore(lightVolumeId, cell, uvec4(lightIndex + 1u, 0u, 0u, 0u));
}

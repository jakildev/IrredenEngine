#version 450 core

layout(local_size_x = 64) in;

#include "ir_iso_common.glsl"

// SYNC: must match kMaxParticlesPerEmitter in ir_render_types.hpp and
// the Metal constant in c_render_stateless_particles_to_trixel.metal.
#define kMaxParticlesPerEmitter 256u

struct GpuParticleEmitter {
    vec3 origin;
    float baseLifetime;
    vec3 baseVelocity;
    float spawnRate;
    vec3 gravity;
    uint baseColor;
    vec3 positionJitter;
    uint emitterFlags;
    vec3 velocityJitter;
    uint particlesPerEmitter;
};

layout(std140, binding = 0) uniform FrameDataStatelessParticles {
    float currentTime;
    uint emitterCount;
    vec2 cameraTrixelOffset;
    ivec2 trixelCanvasOffsetZ1;
    ivec2 canvasSizePixels;
    ivec2 voxelRenderOptions;
    ivec2 _padding;
};

layout(std430, binding = 4) readonly buffer StatelessEmitterBuffer {
    GpuParticleEmitter emitters[];
};

layout(rgba8, binding = 0) writeonly uniform image2D triangleCanvasColors;
layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;

void main() {
    uint gid = gl_GlobalInvocationID.x;
    uint emitterId = gid / kMaxParticlesPerEmitter;
    uint subIndex  = gid % kMaxParticlesPerEmitter;
    if (emitterId >= emitterCount) return;

    GpuParticleEmitter e = emitters[emitterId];
    if (subIndex >= e.particlesPerEmitter) return;

    // A zero spawn rate disables the emitter without producing NaN positions.
    float spawnRateSafe = max(e.spawnRate, 1e-6);
    float spawnOffset = float(subIndex) / spawnRateSafe;
    float ageRaw = currentTime - spawnOffset;
    if (ageRaw < 0.0) return;
    float lifetimeSafe = max(e.baseLifetime, 1e-6);
    float age = mod(ageRaw, lifetimeSafe);
    uint cycle = uint(floor(ageRaw / lifetimeSafe));

    uint seed = hash3(emitterId, subIndex, cycle);
    vec3 jitterPos = e.positionJitter * randomUnitVec(seed);
    vec3 jitterVel = e.velocityJitter * randomUnitVec(seed ^ 0xABCDu);

    vec3 position = e.origin + jitterPos
                  + (e.baseVelocity + jitterVel) * age
                  + 0.5 * e.gravity * age * age;

    const int subdivisions = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    const ivec3 posScaled = roundHalfUp(position * float(subdivisions));

    const ivec2 frameOffset =
        trixelFrameOffset(trixelCanvasOffsetZ1, cameraTrixelOffset, voxelRenderOptions);
    const vec4 baseColor = unpackColor(e.baseColor);
    const ivec2 canvasSize = imageSize(triangleCanvasDistances);

    for (int face = 0; face < 3; face++) {
        for (int u = 0; u < subdivisions; u++) {
            for (int v = 0; v < subdivisions; v++) {
                ivec3 microPos = faceMicroPositionFixed(face, posScaled, u, v, subdivisions);
                int microDepth = pos3DtoDistance(microPos);
                int faceDepth = encodeDepthWithFace(microDepth, face);
                ivec2 microIsoBase = frameOffset + pos3DtoPos2DIso(microPos);
                for (int subPixel = 0; subPixel < 2; subPixel++) {
                    ivec2 canvasPixel = microIsoBase + faceOffset_2x3(face, subPixel);
                    if (!isInsideCanvas(canvasPixel, canvasSize)) continue;
                    int prevDistance = imageAtomicMin(triangleCanvasDistances, canvasPixel, faceDepth);
                    if (faceDepth <= prevDistance) {
                        imageStore(triangleCanvasColors, canvasPixel, baseColor);
                    }
                }
            }
        }
    }
}

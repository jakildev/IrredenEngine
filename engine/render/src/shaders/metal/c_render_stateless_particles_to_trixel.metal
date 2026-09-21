#include "ir_iso_common.metal"

// MSL image atomics use the slot-16 scratch buffer; color writes use the
// post-min value, and same-pixel ties may smear color for one frame.

// SYNC: must match kMaxParticlesPerEmitter in ir_render_types.hpp and
// the GLSL define in c_render_stateless_particles_to_trixel.glsl.
constant uint kMaxParticlesPerEmitter = 256u;

struct GpuParticleEmitter {
    packed_float3 origin;
    float baseLifetime;
    packed_float3 baseVelocity;
    float spawnRate;
    packed_float3 gravity;
    uint baseColor;
    packed_float3 positionJitter;
    uint emitterFlags;
    packed_float3 velocityJitter;
    uint particlesPerEmitter;
};

struct FrameDataStatelessParticles {
    float currentTime;
    uint emitterCount;
    float2 cameraTrixelOffset;
    int2 trixelCanvasOffsetZ1;
    int2 canvasSizePixels;
    int2 voxelRenderOptions;
    int2 _padding;
};

kernel void c_render_stateless_particles_to_trixel(
    constant FrameDataStatelessParticles& frameData [[buffer(0)]],
    device const GpuParticleEmitter* emitters [[buffer(4)]],
    device atomic_int* distanceScratch [[buffer(16)]],
    texture2d<float, access::write> triangleCanvasColors [[texture(0)]],
    texture2d<int, access::write> triangleCanvasDistances [[texture(1)]],
    uint gid [[thread_position_in_grid]]
) {
    const uint emitterId = gid / kMaxParticlesPerEmitter;
    const uint subIndex  = gid % kMaxParticlesPerEmitter;
    if (emitterId >= frameData.emitterCount) return;

    const GpuParticleEmitter e = emitters[emitterId];
    if (subIndex >= e.particlesPerEmitter) return;

    // The guard keeps spawnOffset finite for a non-positive spawnRate. Such a
    // rate leaves only subIndex 0 live (offset 0); every later subIndex gets an
    // offset >= 1e6 s, so it returns on ageRaw < 0 until currentTime passes
    // that. It does not disable the emitter; particlesPerEmitter = 0 does.
    const float spawnRateSafe = max(e.spawnRate, 1e-6f);
    const float spawnOffset = float(subIndex) / spawnRateSafe;
    const float ageRaw = frameData.currentTime - spawnOffset;
    if (ageRaw < 0.0f) return;
    const float lifetimeSafe = max(e.baseLifetime, 1e-6f);
    const float age = fmod(ageRaw, lifetimeSafe);
    const uint cycle = uint(floor(ageRaw / lifetimeSafe));

    const uint seed = hash3(emitterId, subIndex, cycle);
    const float3 jitterPos = float3(e.positionJitter) * randomUnitVec(seed);
    const float3 jitterVel = float3(e.velocityJitter) * randomUnitVec(seed ^ 0xABCDu);

    const float3 position = float3(e.origin) + jitterPos
                          + (float3(e.baseVelocity) + jitterVel) * age
                          + 0.5f * float3(e.gravity) * age * age;

    const int subdivisions = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    const int3 posScaled = roundHalfUp(position * float(subdivisions));

    const int2 frameOffset = trixelFrameOffset(
        frameData.trixelCanvasOffsetZ1,
        frameData.cameraTrixelOffset,
        frameData.voxelRenderOptions
    );
    const float4 baseColor = unpackColor(e.baseColor);

    for (int face = 0; face < 3; face++) {
        for (int u = 0; u < subdivisions; u++) {
            for (int v = 0; v < subdivisions; v++) {
                const int3 microPos = faceMicroPositionFixed(face, posScaled, u, v);
                const int microDepth = pos3DtoDistance(microPos);
                const int faceDepth = encodeDepthWithFace(microDepth, face);
                const int2 microIsoBase = frameOffset + pos3DtoPos2DIso(microPos);
                for (int subPixel = 0; subPixel < 2; subPixel++) {
                    const int2 canvasPixel = microIsoBase + faceOffset_2x3(face, subPixel);
                    if (!isInsideCanvas(canvasPixel, frameData.canvasSizePixels)) continue;
                    const uint linearIndex =
                        uint(canvasPixel.y) * uint(frameData.canvasSizePixels.x) + uint(canvasPixel.x);
                    const int prevDistance = atomic_fetch_min_explicit(
                        &distanceScratch[linearIndex],
                        faceDepth,
                        memory_order_relaxed
                    );
                    if (faceDepth <= prevDistance) {
                        const uint2 pixel = uint2(canvasPixel);
                        triangleCanvasColors.write(baseColor, pixel);
                        triangleCanvasDistances.write(int4(faceDepth, 0, 0, 0), pixel);
                    }
                }
            }
        }
    }
}

#version 460 core

layout(local_size_x = 64) in;

#include "ir_iso_common.glsl"

// T-163 Phase 1 — stateless particle → trixel canvas render pass.
//
// Dispatch fires `emitterCount * kMaxParticlesPerEmitter` threads packed
// into a 1D global invocation index. Each thread derives its
// `(emitterId, subIndex)` from the global id, reads the emitter
// descriptor from the SSBO, and reconstructs its particle's position and
// color from `(emitter, subIndex, currentTime)` via a closed-form
// gravity-with-jitter trajectory:
//
//   spawnOffset = subIndex / spawnRate              (steady-state stagger)
//   ageRaw      = currentTime - spawnOffset
//   age         = mod(ageRaw, baseLifetime)         (cycle-folded age)
//   cycle       = floor(ageRaw / baseLifetime)      (cycle index)
//   seed        = hash3(emitterId, subIndex, cycle) (per-cycle deterministic)
//   pos         = origin + posJitter
//                       + (baseVel + velJitter) * age
//                       + 0.5 * gravity * age * age
//
// No per-particle state, no update pass, no spawn/despawn bookkeeping.
// Every thread is independent. Composites onto the trixel canvas via the
// same `imageAtomicMin` pattern as the T-139 SSBO render path.
//
// Storage split: per-frame inputs live in a small UBO (binding 0); the
// emitter descriptor array is an SSBO (binding 4) so the layout is plain
// `device` storage on Metal — sidesteps the UBO-array layout flakiness
// observed with nested struct arrays during Phase 1 bring-up.
//
// The strict-comparison `voxelDistance <= prevDistance` write decision
// keeps the depth test correct under rare same-pixel collisions; a
// one-frame color smear on a tie is invisible for ambient particle fields.

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

    // spawnRate <= 0 produces a degenerate steady-state stagger (all particles
    // spawn at t=0). Guard so callers can disable an emitter by zeroing the
    // rate without producing NaN positions.
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

    ivec3 posI = ivec3(round(position));
    int particleDistance = encodeDepthWithFace(pos3DtoDistance(posI), kZFace);

    // NONE subdivision mode (matches the T-139 render shader); Phase 1 snaps
    // every particle to the integer trixel lattice.
    ivec2 frameOffset = trixelCanvasOffsetZ1 + ivec2(floor(cameraTrixelOffset));
    ivec2 canvasPixel = frameOffset + pos3DtoPos2DIso(posI);

    if (!isInsideCanvas(canvasPixel, imageSize(triangleCanvasDistances))) return;

    int prevDistance = imageAtomicMin(triangleCanvasDistances, canvasPixel, particleDistance);
    if (particleDistance <= prevDistance) {
        imageStore(triangleCanvasColors, canvasPixel, unpackColor(e.baseColor));
    }
}

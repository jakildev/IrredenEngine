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
// Every thread is independent.
//
// Each particle composites as a full 2×3 voxel diamond — 3 faces × 2
// subpixels per face — using the same `faceOffset_2x3` + face-priority
// depth encoding the voxel and SDF paths use. This is what makes a
// particle read as a voxel in the final composite: the LIGHTING_TO_TRIXEL
// stage decodes the face index from the depth and shades each of the
// three visible faces with its own outward normal, so the iso-projected
// 3-tone face shading kicks in even though the particles never go
// through the C_Voxel pool. Single-pixel writes (the prior Phase 1
// design) would all decode as kZFace and read as flat top-shaded
// pinpricks rather than voxel diamonds.
//
// Storage split: per-frame inputs live in a small UBO (binding 0); the
// emitter descriptor array is an SSBO (binding 4) so the layout is plain
// `device` storage on Metal — sidesteps the UBO-array layout flakiness
// observed with nested struct arrays during Phase 1 bring-up.
//
// The strict-comparison `faceDepth <= prevDistance` write decision
// keeps the depth test correct under rare same-pixel collisions; a
// one-frame color smear on a tie is invisible for ambient particle fields.

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

    // NONE subdivision mode (matches the T-139 render shader); Phase 1 snaps
    // every particle to the integer voxel lattice and emits a full voxel
    // diamond (3 faces × 2 subpixels) per particle.
    ivec3 posI = ivec3(round(position));
    int baseDepth = pos3DtoDistance(posI);

    ivec2 frameOffset = trixelCanvasOffsetZ1 + ivec2(floor(cameraTrixelOffset));
    ivec2 baseCanvasPixel = frameOffset + pos3DtoPos2DIso(posI);
    vec4 baseColor = unpackColor(e.baseColor);
    ivec2 canvasSize = imageSize(triangleCanvasDistances);

    for (int face = 0; face < 3; face++) {
        int faceDepth = encodeDepthWithFace(baseDepth, face);
        for (int subPixel = 0; subPixel < 2; subPixel++) {
            ivec2 canvasPixel = baseCanvasPixel + faceOffset_2x3(face, subPixel);
            if (!isInsideCanvas(canvasPixel, canvasSize)) continue;
            int prevDistance = imageAtomicMin(triangleCanvasDistances, canvasPixel, faceDepth);
            if (faceDepth <= prevDistance) {
                imageStore(triangleCanvasColors, canvasPixel, baseColor);
            }
        }
    }
}

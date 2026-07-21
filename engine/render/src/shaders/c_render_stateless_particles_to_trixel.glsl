#version 450 core

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
// Each particle composites as a subdivision-scaled voxel diamond,
// matching the voxel-pool path step-for-step:
//   posScaled = round(position * subdivisions)
//   for face ∈ {X, Y, Z}, (u, v) ∈ [0, subdivisions)², subPixel ∈ {0, 1}:
//     microPos      = faceMicroPositionFixed(face, posScaled, u, v, sub)
//     canvasPixel   = trixelFrameOffset(...) + pos3DtoPos2DIso(microPos)
//                   + faceOffset_2x3(face, subPixel)
//     depthEncoded  = encodeDepthWithFace(sum(microPos), face)
//     imageAtomicMin into the distance texture; write color on win.
// `voxelRenderOptions` is the same (renderMode, effectiveSubdivisions)
// pair the voxel pipeline reads — so under FULL subdivision mode (the
// default), each particle expands to a sub² × 6-trixel diamond just
// like a voxel does, and the lighting pass's per-face normal shading
// reads identically across particles, voxels, and SDFs. Without this,
// particles stayed at the base 6-trixel resolution while voxels refined
// to sub² micro-cells, so the two read as different sizes in the same
// frame and zooming amplified the disparity. At sub == 1 (NONE mode)
// the loop collapses to the prior 3-face × 2-subpixel diamond.
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

    // Scale the particle position into the same fixed-point grid the voxel
    // pool uses under FULL subdivision mode. At sub == 1 (NONE) this is a
    // plain integer round; at sub > 1 the particle gets sub-voxel
    // positional precision matching the voxel-pool `position * sub` cast
    // in c_voxel_to_trixel_stage_1.glsl.
    const int subdivisions = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    const ivec3 posScaled = roundHalfUp(position * float(subdivisions));

    const ivec2 frameOffset =
        trixelFrameOffset(trixelCanvasOffsetZ1, cameraTrixelOffset, voxelRenderOptions);
    const vec4 baseColor = unpackColor(e.baseColor);
    const ivec2 canvasSize = imageSize(triangleCanvasDistances);

    // Walk each face's sub × sub micro grid. Mirrors the voxel-pool dispatch
    // where numGroupsZ = sub² (one workgroup per (u, v)) and the (2, 3, 1)
    // workgroup writes (face, subPixel). We collapse the GPU-parallel shape
    // into an inner loop because particle counts are smaller than voxel
    // counts (kMaxStatelessEmitters * kMaxParticlesPerEmitter ≈ 16K worst
    // case) and the loop has fixed bounds the compiler can unroll.
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

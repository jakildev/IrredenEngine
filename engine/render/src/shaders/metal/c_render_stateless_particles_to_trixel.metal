#include "ir_iso_common.metal"

// T-163 Phase 1 — stateless particle → trixel canvas render pass (Metal).
// Mirrors c_render_stateless_particles_to_trixel.glsl. Each thread derives
// its (emitterId, subIndex) from the global thread id, reads the emitter
// descriptor from the SSBO at [[buffer(4)]], and reconstructs the
// particle's position from a closed-form gravity-with-jitter trajectory.
//
// Each particle emits a subdivision-scaled voxel diamond, mirroring the
// voxel-pool path: posScaled = round(position * sub) → for (face, u, v,
// subPixel) walk faceMicroPositionFixed + faceOffset_2x3 → atomicMin on
// the distance scratch buffer. `voxelRenderOptions` carries the same
// (renderMode, effectiveSubdivisions) pair the voxel pipeline reads;
// at sub > 1 (FULL mode, default) each particle expands into sub² × 6
// trixels just like a voxel does. See the GLSL twin for the full
// rationale.
//
// Same MSL image-atomic workaround as the voxel-to-trixel and T-139 render
// stages: distance writes go through a `device atomic_int*` scratch buffer
// (slot 16), and the color write reads back the post-min value to decide
// whether this particle won the depth test. Race semantics match GLSL —
// same-pixel collisions can produce a one-frame color smear, invisible for
// ambient particle fields.

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

    // Scale the particle position into the same fixed-point grid the voxel
    // pool uses under FULL subdivision mode (sub > 1 → sub-voxel precision;
    // sub == 1 collapses to plain integer rounding).
    const int subdivisions = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    const int3 posScaled = roundHalfUp(position * float(subdivisions));

    const int2 frameOffset = trixelFrameOffset(
        frameData.trixelCanvasOffsetZ1,
        frameData.cameraTrixelOffset,
        frameData.voxelRenderOptions
    );
    const float4 baseColor = unpackColor(e.baseColor);

    // Walk each face's sub × sub micro grid (matches the voxel-pool dispatch
    // shape collapsed into an inner loop; see GLSL twin for the rationale on
    // why the loop fits the particle-count budget at fixed bounds).
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

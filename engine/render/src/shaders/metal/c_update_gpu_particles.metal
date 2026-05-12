#include <metal_stdlib>
using namespace metal;

// T-139 Phase 1 / T-159 Phase 2 batching — GPU particle update pass (Metal).
// Mirrors c_update_gpu_particles.glsl; see the .glsl counterpart for the
// pending-list spawn-path design note. One threadgroup-thread per particle
// slot; dead slots early-out. Per-particle integration: position +=
// velocity*dt, lifetime -= dt.

struct FrameDataGpuParticles {
    float deltaTime;
    uint particleCount;
    uint _updatePad0;
    uint _updatePad1;
    float2 _renderCameraOffset;
    int2 _renderTrixelOffset;
    int2 _renderCanvasSize;
    int _renderPad0;
    int _renderPad1;
};

struct Particle {
    packed_float3 position;
    float lifetime;
    packed_float3 velocity;
    uint color;
};

kernel void c_update_gpu_particles(
    constant FrameDataGpuParticles& frameData [[buffer(23)]],
    device Particle* particles [[buffer(4)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid >= frameData.particleCount) return;

    Particle p = particles[gid];
    if (p.lifetime <= 0.0) return;

    p.position = float3(p.position) + float3(p.velocity) * frameData.deltaTime;
    p.lifetime -= frameData.deltaTime;
    if (p.lifetime < 0.0) {
        p.lifetime = 0.0;
    }

    particles[gid] = p;
}

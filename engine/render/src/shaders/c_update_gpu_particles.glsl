#version 460 core

layout(local_size_x = 64) in;

// T-139 Phase 1 — GPU particle update pass.
// One thread per particle slot; dead slots (lifetime <= 0) early-out so the
// pool can hold a high-water-mark capacity even when the live count is small.
// Drift is integrated by `velocity * deltaTime`; lifetime decays by deltaTime.
// Spawning is CPU-side: `C_GPUParticlePool::writeSlot` pushes a single slot
// to this SSBO via subData at spawn time, so the GPU is the source of truth
// between mutations. A future phase adds a GPU spawn pass for compute-driven
// emitters.

layout(std140, binding = 23) uniform FrameDataGpuParticles {
    float deltaTime;
    uint particleCount;
    uint _updatePad0;
    uint _updatePad1;
    vec2 _renderCameraOffset;
    ivec2 _renderTrixelOffset;
    ivec2 _renderCanvasSize;
    int _renderPad0;
    int _renderPad1;
};

struct Particle {
    vec3 position;
    float lifetime;
    vec3 velocity;
    uint color;
};

layout(std430, binding = 4) buffer ParticleBuffer {
    Particle particles[];
};

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= particleCount) return;

    Particle p = particles[idx];
    if (p.lifetime <= 0.0) return;

    p.position += p.velocity * deltaTime;
    p.lifetime -= deltaTime;
    if (p.lifetime < 0.0) {
        p.lifetime = 0.0;
    }

    particles[idx] = p;
}

#version 450 core

layout(local_size_x = 64) in;

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

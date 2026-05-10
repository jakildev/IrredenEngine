#version 460 core

layout(local_size_x = 64) in;

#include "ir_iso_common.glsl"

// T-139 Phase 1 — GPU particle → trixel canvas render pass.
// One thread per particle slot; dead slots early-out. Each live particle
// projects its world position into iso 2D, computes a depth scalar, and
// writes a single trixel (color + distance) into the canvas textures using
// the same atomicMin pattern as the voxel-to-trixel stages.
//
// Single-pass write (no two-stage compaction): particles are sparse and the
// expected per-pixel collision rate is low (a 4096-particle pool over a
// ~256k-pixel canvas is ~1.6% pixel density). The strict-comparison
// `voxelDistance <= prevDistance` write decision keeps the depth-test
// correct under rare collisions; on a same-pixel race the worst case is a
// one-frame color smear, which is invisible for ambient particle fields.

layout(std140, binding = 23) uniform FrameDataGpuParticles {
    float _updateDeltaTime;
    uint particleCount;
    uint _updatePad0;
    uint _updatePad1;
    vec2 cameraTrixelOffset;
    ivec2 trixelCanvasOffsetZ1;
    ivec2 canvasSizePixels;
    int _renderPad0;
    int _renderPad1;
};

struct Particle {
    vec3 position;
    float lifetime;
    vec3 velocity;
    uint color;
};

layout(std430, binding = 4) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(rgba8, binding = 0) writeonly uniform image2D triangleCanvasColors;
layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= particleCount) return;

    Particle p = particles[idx];
    if (p.lifetime <= 0.0) return;

    ivec3 posI = ivec3(round(p.position));
    int particleDistance = encodeDepthWithFace(pos3DtoDistance(posI), kZFace);

    // Use NONE subdivision mode (voxelRenderOptions.x == 0) — Phase 1 keeps
    // particles snapped to the integer trixel lattice. Subdivision support
    // matches voxels and lands in a follow-up.
    ivec2 frameOffset = trixelCanvasOffsetZ1 + ivec2(floor(cameraTrixelOffset));
    ivec2 canvasPixel = frameOffset + pos3DtoPos2DIso(posI);

    if (!isInsideCanvas(canvasPixel, imageSize(triangleCanvasDistances))) return;

    int prevDistance = imageAtomicMin(triangleCanvasDistances, canvasPixel, particleDistance);
    if (particleDistance <= prevDistance) {
        imageStore(triangleCanvasColors, canvasPixel, unpackColor(p.color));
    }
}

#include "ir_iso_common.metal"

// T-139 Phase 1 — GPU particle → trixel canvas render pass (Metal). Mirrors
// c_render_gpu_particles_to_trixel.glsl. One thread per particle slot; dead
// slots early-out. Each live particle projects to iso 2D and writes a single
// trixel into the canvas color + distance textures.
//
// Same MSL image-atomic workaround as the voxel-to-trixel stages: distance
// writes go through a `device atomic_int*` scratch buffer (slot 16), and the
// color write reads back the post-min value to decide whether this particle
// won the depth test. Race semantics match the GLSL version — same-pixel
// collisions can produce a one-frame color smear, invisible for ambient
// particle fields.

struct FrameDataGpuParticles {
    float _updateDeltaTime;
    uint particleCount;
    uint _updatePad0;
    uint _updatePad1;
    float2 cameraTrixelOffset;
    int2 trixelCanvasOffsetZ1;
    int2 canvasSizePixels;
    int _renderPad0;
    int _renderPad1;
};

struct Particle {
    packed_float3 position;
    float lifetime;
    packed_float3 velocity;
    uint color;
};

inline float4 unpackParticleColor(uint packedColor) {
    return float4(
        float(packedColor & 0xFFu) / 255.0,
        float((packedColor >> 8) & 0xFFu) / 255.0,
        float((packedColor >> 16) & 0xFFu) / 255.0,
        float((packedColor >> 24) & 0xFFu) / 255.0
    );
}

kernel void c_render_gpu_particles_to_trixel(
    constant FrameDataGpuParticles& frameData [[buffer(23)]],
    device const Particle* particles [[buffer(4)]],
    device atomic_int* distanceScratch [[buffer(16)]],
    texture2d<float, access::write> triangleCanvasColors [[texture(0)]],
    texture2d<int, access::write> triangleCanvasDistances [[texture(1)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid >= frameData.particleCount) return;

    Particle p = particles[gid];
    if (p.lifetime <= 0.0) return;

    int3 posI = int3(round(float3(p.position)));
    int particleDistance = encodeDepthWithFace(pos3DtoDistance(posI), kZFace);

    int2 frameOffset = frameData.trixelCanvasOffsetZ1 + int2(floor(frameData.cameraTrixelOffset));
    int2 canvasPixel = frameOffset + pos3DtoPos2DIso(posI);

    if (!isInsideCanvas(canvasPixel, frameData.canvasSizePixels)) return;

    const uint linearIndex =
        uint(canvasPixel.y) * uint(frameData.canvasSizePixels.x) + uint(canvasPixel.x);
    int prevDistance = atomic_fetch_min_explicit(
        &distanceScratch[linearIndex],
        particleDistance,
        memory_order_relaxed
    );
    if (particleDistance <= prevDistance) {
        const uint2 pixel = uint2(canvasPixel);
        triangleCanvasColors.write(unpackParticleColor(p.color), pixel);
        triangleCanvasDistances.write(int4(particleDistance, 0, 0, 0), pixel);
    }
}

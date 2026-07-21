#include "ir_iso_common.metal"

// T-139 Phase 1 — GPU particle → trixel canvas render pass (Metal). Mirrors
// c_render_gpu_particles_to_trixel.glsl. One thread per particle slot; dead
// slots early-out. Each live particle emits a 6-trixel voxel-diamond
// (3 faces × 2 sub-pixels) using faceOffset_2x3 + face-priority depth
// encoding so LIGHTING_TO_TRIXEL can shade each face with its own outward
// normal. Phase 1 uses NONE subdivision mode (subdivisions = 1).
//
// Same MSL image-atomic workaround as the voxel-to-trixel and stateless-
// particles stages: distance writes go through a `device atomic_int*` scratch
// buffer (slot 16), and the color write reads back the post-min value to
// decide whether this particle won the depth test. Race semantics match the
// GLSL version — same-pixel collisions can produce a one-frame color smear,
// invisible for ambient particle fields.

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

    const int3 posI = roundHalfUp(float3(p.position));
    const int2 frameOffset = frameData.trixelCanvasOffsetZ1 + int2(floor(frameData.cameraTrixelOffset));
    const float4 baseColor = unpackColor(p.color);

    for (int face = 0; face < 3; face++) {
        const int3 microPos = faceMicroPositionFixed(face, posI, 0, 0);
        const int faceDepth = encodeDepthWithFace(pos3DtoDistance(microPos), face);
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

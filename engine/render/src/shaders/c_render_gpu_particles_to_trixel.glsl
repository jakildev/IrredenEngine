#version 450 core

layout(local_size_x = 64) in;

#include "ir_iso_common.glsl"

// T-139 Phase 1 — GPU particle → trixel canvas render pass.
// One thread per particle slot; dead slots early-out. Each live particle
// projects its world position into iso 2D and emits a 6-trixel voxel-diamond
// (3 faces × 2 sub-pixels) using faceOffset_2x3 + face-priority depth
// encoding, so LIGHTING_TO_TRIXEL can shade each face with its own outward
// normal. Mirrors the loop shape in c_render_stateless_particles_to_trixel.
//
// Phase 1 uses NONE subdivision mode (subdivisions = 1): each particle maps
// to exactly one 2×3 trixel diamond. Sub-voxel positional precision and
// full FULL-mode expansion to match voxels land in a follow-up.
//
// Single-pass write (no two-stage compaction): particles are sparse and the
// expected per-pixel collision rate is low. The strict-comparison
// `faceDepth <= prevDistance` write decision keeps the depth test correct
// under rare same-pixel collisions; a one-frame color smear on a tie is
// invisible for ambient particle fields.

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

    const ivec3 posI = roundHalfUp(p.position);
    const ivec2 frameOffset = trixelCanvasOffsetZ1 + ivec2(floor(cameraTrixelOffset));
    const vec4 baseColor = unpackColor(p.color);
    const ivec2 canvasSize = imageSize(triangleCanvasDistances);

    for (int face = 0; face < 3; face++) {
        ivec3 microPos = faceMicroPositionFixed(face, posI, 0, 0, 1);
        int faceDepth = encodeDepthWithFace(pos3DtoDistance(microPos), face);
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

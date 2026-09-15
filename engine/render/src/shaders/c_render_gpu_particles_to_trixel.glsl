#version 450 core

layout(local_size_x = 64) in;

#include "ir_iso_common.glsl"

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

// Test-local probe kernel for the fog line-of-sight smooth gate
// (test/render/fog_cross_section_test.cpp, GpuSmoothOcclusionMatchesTheCpuOracle).
//
// Includes the REAL gate (ir_fog_los.glsl) and reveal curve (ir_iso_common.glsl).
// Sample arm: for every float position, the smooth visibility and the gated
// reveal the fog kernel's source loop computes for source 0. Face arm: for
// every enumerated side-face pixel, the chain c_fog_to_trixel's
// fogLosPixelFaceSample runs from the world face on — the view face, the recovered
// line-of-sight voxel, the fogLosFaceSample column and height, and that
// sample's smooth visibility for source 0.

#version 450 core
#include "../../../engine/render/src/shaders/ir_iso_common.glsl"
#define IR_FOG_LOS_BINDING 0
#include "../../../engine/render/src/shaders/ir_fog_los.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// std430: vec4 at 0, then four scalars at 16, the vec4 array at 32.
layout(std430, binding = 2) readonly buffer FogLosSmoothProbeIn {
    vec4 circle;
    float softness;
    int sampleCount;
    int faceCount;
    int _probePad0;
    vec4 samplePositions[];
};

// Per face pixel: (isoRel.x, isoRel.y, rawDepth, worldFaceId),
// (scale, microFaces, cardinalIndex, unused).
layout(std430, binding = 3) readonly buffer FogLosFaceProbeIn {
    ivec4 facePixels[];
};

layout(std430, binding = 1) writeonly buffer FogLosSmoothProbeOut {
    vec4 sampleResults[];
};

// Per face pixel: (voxel.xyz, viewFaceId), (cell.xy, bits(z), bits(visibility)).
layout(std430, binding = 4) writeonly buffer FogLosFaceProbeOut {
    ivec4 faceResults[];
};

void main() {
    const int index = int(gl_GlobalInvocationID.x);
    if (index < sampleCount) {
        const vec3 position = samplePositions[index].xyz;
        const FogLosSample s = fogLosSurfaceSample(position);
        const FogLosTaps taps = fogLosLoadTaps(s, 0);
        const float visibility = fogLosSmoothVisibility(taps, 0, s, softness);
        sampleResults[index] =
            vec4(visibility, visibility * fogVisionCircleReveal(position.xy, circle, 0.0), 0.0, 0.0);
    }
    if (index < faceCount) {
        const ivec4 pixel = facePixels[2 * index];
        const ivec4 raster = facePixels[2 * index + 1];
        const int viewFaceId = rotateFaceIdCardinalZ(pixel.w, raster.z);
        const ivec3 voxel =
            fogLosFaceVoxel(pixel.xy, pixel.z, viewFaceId, raster.x, raster.y != 0, raster.z);
        const FogLosSample s = fogLosFaceSample(voxel, pixel.w);
        const float visibility = fogLosSmoothVisibility(fogLosLoadTaps(s, 0), 0, s, softness);
        faceResults[2 * index] = ivec4(voxel, viewFaceId);
        faceResults[2 * index + 1] =
            ivec4(s.cell, floatBitsToInt(s.z), floatBitsToInt(visibility));
    }
}

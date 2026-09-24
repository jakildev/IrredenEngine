// Test-local probe kernel for the fog line-of-sight gate
// (test/render/fog_cross_section_test.cpp, GpuOcclusionMatchesTheCpuOracle).
//
// Includes the REAL gate (ir_fog_los.glsl) and the real reveal curve
// (ir_iso_common.glsl's fogVisionCircleReveal), and for every probed column and
// sample height writes the gate verdict and the gated reveal the fog kernel's
// source loop computes for source 0, so a one-sided edit to the gate changes
// what this kernel returns.

#version 450 core
#include "../../../engine/render/src/shaders/ir_iso_common.glsl"
#define IR_FOG_LOS_BINDING 0
#include "../../../engine/render/src/shaders/ir_fog_los.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Mirrored by kLosProbeHalfExtent / kLosProbeDim / kLosProbeLevels in
// fog_cross_section_test.cpp.
const int kProbeHalfExtent = 32;
const int kProbeDim = kProbeHalfExtent * 2;
const int kProbeLevels = 3;

// std430: vec4 at 0, four ints at 16, the int array at 32.
layout(std430, binding = 2) readonly buffer FogLosProbeIn {
    vec4 circle;
    int losSourceMask;
    int _probePad0;
    int _probePad1;
    int _probePad2;
    int sampleZ[];
};

struct FogLosProbe {
    int visible;
    float reveal;
};

layout(std430, binding = 1) writeonly buffer FogLosProbeOut {
    FogLosProbe probes[];
};

void main() {
    const ivec2 idx = ivec2(gl_GlobalInvocationID.xy);
    if (idx.x >= kProbeDim || idx.y >= kProbeDim) {
        return;
    }
    const ivec2 cell = idx - ivec2(kProbeHalfExtent);
    for (int level = 0; level < kProbeLevels; ++level) {
        const int record = (level * kProbeDim + idx.y) * kProbeDim + idx.x;
        const ivec3 sampleVoxel = ivec3(cell, sampleZ[record]);
        const bool visible = fogLosVisible(sampleVoxel, 0);
        probes[record].visible = visible ? 1 : 0;
        probes[record].reveal = fogLosSourceGated(losSourceMask, 0) && !visible
            ? 0.0
            : fogVisionCircleReveal(vec2(cell), circle, 0.0);
    }
}

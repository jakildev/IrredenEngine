// Test-local probe kernel for the shared FIELD reveal
// (test/render/fog_cross_section_test.cpp, GpuRevealSurfaceMatchesTheCpuOracle).
//
// Includes the REAL shared reveal (ir_fog_common.glsl) — the per-source
// ceiling, line-of-sight, column-top and reveal-surface treatment composition
// every fog paint route runs — and evaluates it at each probed world position
// with the sample itself as the line-of-sight target and the host's column
// point and face axis, writing the FogReveal the colour apply would read. The
// grid image, the observer block and the line-of-sight image take the fog
// pass's own bindings, so the host uploads the engine's observer struct
// verbatim and a std140 drift that would break the real path breaks this
// probe too.

#version 450 core
#include "../../../engine/render/src/shaders/ir_iso_common.glsl"
#define IR_FOG_LOS_BINDING 4
#include "../../../engine/render/src/shaders/ir_fog_common.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct FogSurfaceProbeSample {
    // (x, y, z, face axis)
    vec4 position;
    // (x, y, z, 0)
    vec4 columnPoint;
};

// std430: four ints at 0, the sample array at 16.
layout(std430, binding = 2) readonly buffer FogSurfaceProbeIn {
    int sampleCount;
    int _surfacePad0;
    int _surfacePad1;
    int _surfacePad2;
    FogSurfaceProbeSample samples[];
};

layout(std430, binding = 1) writeonly buffer FogSurfaceProbeOut {
    // (state, gridState, hardDistPastRim, styledBand)
    vec4 probes[];
};

layout(std430, binding = 3) writeonly buffer CeilingProbeOut {
    vec4 ceilingProbes[];
};

void main() {
    const int index = int(gl_GlobalInvocationID.x);
    if (index >= sampleCount) {
        return;
    }
    const vec3 position = samples[index].position.xyz;
    const FogReveal reveal = fogRevealSample(
        position,
        position,
        samples[index].columnPoint.xyz,
        int(samples[index].position.w),
        0.0,
        true
    );
    const ivec3 voxel = roundHalfUp(position);
    const uvec2 cell = fogTapTexel(voxel.xy, imageSize(canvasFogOfWar));
    ceilingProbes[index] = vec4(
        fogCeilingPresence(voxel, cell, visionCircles, visionCircleHeights,
            visionCircleCeilings, visionCircleChannels, visionCircleCount, 0.0),
        fogCeilingRetained(voxel, cell, visionCircles, visionCircleHeights,
            visionCircleCeilings, visionCircleChannels, visionCircleCount, revealSurfaceTreatment, 0.0) ? 1.0 : 0.0,
        fogCeilingRetained(voxel, cell, visionCircles, visionCircleHeights,
            visionCircleCeilings, visionCircleChannels, visionCircleCount, revealSurfaceTreatment, 0.37) ? 1.0 : 0.0,
        fogCeilingPhaseHeight(voxel, 0.37));
    probes[index] =
        vec4(reveal.state, reveal.gridState, reveal.hardDistPastRim, reveal.styledBand);
}

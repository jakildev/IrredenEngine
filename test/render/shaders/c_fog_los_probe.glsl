// Test-local probe kernel for the fog line-of-sight gate
// (test/render/fog_cross_section_test.cpp, GpuOcclusionMatchesTheCpuOracle).
//
// Includes the REAL gate (ir_fog_los.glsl) and the real reveal curve
// (ir_iso_common.glsl's fogVisionCircleReveal). For every probed sample it
// writes the gate's visibility over the uploaded column field, the gated
// reveal the fog kernel's source loop computes for source 0, and the
// canonical sample the cardinal route would evaluate for a face pixel at that
// position — so a one-sided edit to the gate changes what this kernel
// returns. The field is anchored with the fog window named in the header, as
// the fog kernel anchors it.

#version 450 core
#include "../../../engine/render/src/shaders/ir_iso_common.glsl"
#define IR_FOG_LOS_BINDING 0
#include "../../../engine/render/src/shaders/ir_fog_los.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// std430: vec4 at 0, vec4 at 16, eight ints at 32, the sample array at 64.
layout(std430, binding = 2) readonly buffer FogLosProbeIn {
    vec4 circle;
    // (observerZ, eyeHeight, softness, subdivisions)
    vec4 source;
    int losSourceMask;
    int sampleCount;
    int windowOriginX;
    int windowOriginY;
    int windowEdge;
    int _probePad0;
    int _probePad1;
    int _probePad2;
    // (x, y, z, faceId) — a negative faceId probes the point itself.
    vec4 samples[];
};

struct FogLosProbe {
    float visibility;
    float reveal;
    float clearance;
    float bandClearance;
    vec4 canonical;
};

layout(std430, binding = 1) writeonly buffer FogLosProbeOut {
    FogLosProbe probes[];
};

void main() {
    const int index = int(gl_GlobalInvocationID.x);
    if (index >= sampleCount) {
        return;
    }
    const vec4 probed = samples[index];
    const int faceId = int(probed.w);
    const vec3 target = faceId < 0
        ? probed.xyz
        : fogLosCanonicalSample(probed.xyz, faceId, kFogLosRouteCardinal, int(source.w));
    const vec3 eye = fogLosEye(circle, source.x, source.y);
    const ivec2 fieldMin = fogLosFieldMin(ivec2(windowOriginX, windowOriginY), windowEdge);
    float bandClearance;
    const float clearance = fogLosTraceClearance(eye, target, source.z, fieldMin, bandClearance);
    const float visibility = fogLosVisibilityFromClearance(clearance, bandClearance, source.z);
    const bool gated = fogLosSourceGated(losSourceMask, 0) &&
        length(target.xy - circle.xy) <= fogLosReach(circle);
    probes[index].visibility = gated ? visibility : 1.0;
    probes[index].reveal =
        (gated ? visibility : 1.0) * fogVisionCircleReveal(target.xy, circle, 0.0);
    probes[index].clearance = clearance;
    probes[index].bandClearance = bandClearance;
    probes[index].canonical = vec4(target, 0.0);
}

// Test-local probe kernel for the #2102 per-fragment fog clip
// (test/render/fog_cross_section_test.cpp, issue #2107).
//
// It is a wrapper in exactly the shape of c_voxel_to_trixel_stage_1.glsl — same
// include chain, same IR_VOXEL_FOG_GRID_BINDING slot — but instead of the stage
// body it evaluates the three fog curves the clip is built from, once per world
// grid COLUMN, into an SSBO the host reads back:
//
//   * fogColumnReveal        — the z-free own-column curve (cut-face test)
//   * fogColumnRevealNearest — the shipped stage-1 KEEP metric (drop iff <= 0)
//   * fogVisionCircleReveal  — the analytic curve FOG_TO_TRIXEL evaluates
//                              per PIXEL, sampled here at the column centre and
//                              at the cell point nearest the circle
//
// Probing the REAL definitions (this file includes ir_voxel_face_select.glsl,
// not a copy) is the whole point: a one-sided edit to the clip changes what
// this kernel returns. The stage body is deliberately NOT included — it needs
// the full voxel-pool binding set, and every property tests A–E assert is a
// property of these curves, not of the raster around them.
//
// The #2260 Z twins (fogColumnRevealZ / fogColumnRevealNearestZ) live in the
// stage body rather than the shared include, so they are out of reach here.
// With all-zero visionCircleHeights — the default, and what the host uploads —
// they are bit-identical to the z-free twins probed below.

#version 450 core
#include "../../../engine/render/src/shaders/ir_iso_common.glsl"
#define IR_VOXEL_FOG_GRID_BINDING 0
#include "../../../engine/render/src/shaders/ir_constants.glsl"
#include "../../../engine/render/src/shaders/ir_voxel_face_select.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Mirrored by kProbeHalfExtent / kProbeDim in fog_cross_section_test.cpp: the
// probed columns are [-kProbeHalfExtent, kProbeHalfExtent) on both axes. A
// divergence shows up as a row of untouched sentinel values in the readback.
const int kProbeHalfExtent = 32;
const int kProbeDim = kProbeHalfExtent * 2;

// One record per probed column. std430, four floats — no padding, so the CPU
// mirror is a plain struct of four floats.
struct FogColumnProbe {
    float revealCenter;
    float revealNearest;
    float revealFloorCenter;
    float revealFloorNearest;
};

layout(std430, binding = 1) writeonly buffer FogProbeOut {
    FogColumnProbe probes[];
};

void main() {
    const ivec2 idx = ivec2(gl_GlobalInvocationID.xy);
    if (idx.x >= kProbeDim || idx.y >= kProbeDim) {
        return;
    }
    const ivec2 col = idx - ivec2(kProbeHalfExtent);
    const int record = idx.y * kProbeDim + idx.x;

    probes[record].revealCenter = fogColumnReveal(col);
    probes[record].revealNearest = fogColumnRevealNearest(col);

    // FOG_TO_TRIXEL's per-pixel curve, sampled at two points inside this
    // column's unit cell. `aa` is 0 here (not the pass's worldPerPixel) so the
    // sample is the zoom-independent hard-disc form the object clip uses — the
    // shared-curve identity tests D asserts is between THESE evaluations.
    const vec4 circle = visionCircles[0];
    probes[record].revealFloorCenter = fogVisionCircleReveal(vec2(col), circle, 0.0);
    const vec2 nearest =
        clamp(circle.xy, vec2(col) - kFogColumnCellHalf, vec2(col) + kFogColumnCellHalf);
    probes[record].revealFloorNearest = fogVisionCircleReveal(nearest, circle, 0.0);
}

/*
 * Project: Irreden Engine
 * File: f_peraxis_scatter.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: May 2026
 * -----
 * Smooth camera Z-yaw — T3 (#1310) forward-scatter composite.
 * Writes the scattered face quad's color + depth into the shared framebuffer;
 * the GL_LESS depth test composites the three per-axis canvases per pixel.
 */

#version 450 core

flat in vec4 vColor;
// Per-fragment planar depth + margin-yield classification (#1457): vDepth is
// the face plane's exact depth at this fragment (linear interpolation of
// per-corner planar keys); fragments outside the exact [0,1]^2 footprint are
// conservative-dilation margin and yield by vMarginDepthBias, so a margin
// only fills pixels no exact footprint claims (the #1494 sub-pixel sliver
// gaps) and never beats a same-plane owner via draw order.
noperspective in float vDepth;
noperspective in vec2 vQuadParam;
flat in float vMarginDepthBias;
// Per-axis margin-yield slope (#1883), in vDepth units per unit of quad-param
// penetration. A large per-axis margin extrapolates the face plane far enough
// that its depth would beat a neighbor face's exact footprint across a shared
// ridge; scaling the yield by penetration * this slope makes the margin yield in
// proportion to its own extrapolation excursion (the doubled top<->side sliver).
flat in float vMarginYieldGradU;
flat in float vMarginYieldGradV;
// Face-center iso-depth for per-face depth-color (#1697). Flat (constant across
// the quad) — origin is the same for all 4 corners of a face instance, so
// interpolation would be a no-op anyway and flat avoids shader-pipeline
// divergence from adding a smooth varying.
flat in float vIsoDepth;
flat in int vDepthColorMode;
flat in float vDepthColorExtent;
// Deterministic sub-band tiebreak (#2255/#2411) — see v_peraxis_scatter.glsl:
// the fragment's final depth is quantized to the tie band below and this
// 4-bit priority-major code ((rank2 << 2) | cell2, pre-scaled to step units)
// is injected into the sub-band bits, so tie-band fragments resolve by slot
// rank then cell identity instead of the #1961 compaction's run-variant draw
// order.
flat in float vCellTieOffset;
// Mirror of kScatterCellTieBand in ir_iso_common.glsl (this fragment stage
// has no common include). Band = 16 x kScatterCellTieStep; power-of-two so
// the floor-quantization below is exact in float32. Keep in sync. 16 is the
// unique width satisfying both (mutually-opposed) halves of the tie-order
// precondition documented at kScatterCellTieStep in ir_iso_common.glsl and
// asserted CPU-side in ir_render_types.hpp — retuning it here alone would
// silently break margin-vs-exact ordering.
const float kScatterCellTieBand = 16.0 / 8388608.0;

out vec4 FragColor;

// HSV → RGB. Identical to hsvToRgb in c_shapes_to_trixel.glsl — voxel-scatter
// depth-color is bit-exact with the SDF twin when mode is on (#1697).
vec3 hsvToRgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    if (vColor.a < 0.1) {
        discard;
    }
    if (vDepthColorMode != 0) {
        float dColor = vDepthColorExtent;
        float denomC = max((4.0 / 3.0) * dColor, 1.0);
        float t = clamp((vIsoDepth + dColor) / denomC, 0.0, 1.0);
        FragColor = vec4(hsvToRgb(vec3(0.66 * t, 1.0, 1.0)), 1.0);
    } else {
        FragColor = vColor;
    }
    const bool inMargin = any(lessThan(vQuadParam, vec2(0.0))) ||
                          any(greaterThan(vQuadParam, vec2(1.0)));
    // Penetration past the exact [0,1]^2 footprint (per axis, >= 0). A margin
    // fragment yields by the flat bias PLUS penetration * per-axis yield slope, so
    // a cell-deep margin (whose plane extrapolation gained a real depth advantage)
    // yields the shared ridge to the neighbor face's exact footprint, while a
    // sub-pixel gap-fill yields almost nothing and still wins (#1883).
    const vec2 outside = max(max(-vQuadParam, vQuadParam - vec2(1.0)), vec2(0.0));
    const float yieldBias =
        vMarginDepthBias + outside.x * vMarginYieldGradU + outside.y * vMarginYieldGradV;
    // #2255: band-quantize + cell-code injection (see vCellTieOffset above).
    // Exact power-of-two float ops, so same-band fragments from different
    // cells land on bit-distinct, cell-ordered depths on every backend.
    const float scatterDepth = vDepth + (inMargin ? yieldBias : 0.0);
    gl_FragDepth =
        floor(scatterDepth / kScatterCellTieBand) * kScatterCellTieBand + vCellTieOffset;
}

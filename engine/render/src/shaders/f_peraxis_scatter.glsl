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

// Sole consumer here is kScatterCellTie{Step,Band} — the same include the
// sibling fragment gather (f_trixel_to_framebuffer.glsl) already uses, so the
// tie band has exactly one GLSL definition site.
#include "ir_iso_common.glsl"

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
// Interior-edge yield-slope floor + flat interior yield (#2428) — see
// v_peraxis_scatter.glsl. A margin that penetrates an INTERIOR edge extends over
// the ADJACENT visible face, so its slope is floored at the cross-face divergence
// bound and it pays a flat bias covering the constant (flip << 2) | slot key gap;
// boundary (silhouette) penetrations keep the tighter own-slope yield.
flat in float vMarginYieldGradFloor;
flat in float vMarginInteriorYieldBias;
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
// Per-edge interior/boundary classification for the analytic coverage (#1937) —
// .x = u-low, .y = u-high, .z = v-low, .w = v-high; 1 = interior, 0 = silhouette.
flat in vec4 vEdgeInterior;

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
    // Analytic edge-aware coverage (#1937, epic #1933 root fix). The visit-bound
    // dilation only guarantees this fragment was VISITED; the coverage DECISION
    // is here, from the fragment's
    // position in the true [0,1]^2 footprint (vQuadParam) and its per-edge
    // interior/boundary flags. Hard-thresholded for the depth co-sort write (no
    // alpha blend). This removes the #1883 near-cardinal corner spikes and
    // foreshortened-silhouette dashing at the source. fwidth() before any
    // non-uniform discard so the derivative is valid (the alpha test above is on a
    // flat varying — uniform across the instance).
    const float coverage =
        scatterAnalyticEdgeCoverage(vQuadParam, fwidth(vQuadParam), vEdgeInterior);
    if (coverage < 0.5) {
        discard;
    }
    const bool inMargin = any(lessThan(vQuadParam, vec2(0.0))) ||
                          any(greaterThan(vQuadParam, vec2(1.0)));
    if (vDepthColorMode == -1) {
        // Margin-classification overlay (#2428): bright = margin fragment
        // (conservative-dilation fill outside the exact footprint), dim =
        // exact-footprint fragment. Axis hue from the vertex stage.
        FragColor = vec4(vColor.rgb * (inMargin ? 1.0 : 0.4), 1.0);
    } else if (vDepthColorMode != 0) {
        float dColor = vDepthColorExtent;
        float denomC = max((4.0 / 3.0) * dColor, 1.0);
        float t = clamp((vIsoDepth + dColor) / denomC, 0.0, 1.0);
        FragColor = vec4(hsvToRgb(vec3(0.66 * t, 1.0, 1.0)), 1.0);
    } else {
        FragColor = vColor;
    }
    // Penetration past the exact [0,1]^2 footprint (per axis, >= 0). A margin
    // fragment yields by the flat bias PLUS penetration * per-axis yield slope, so
    // a cell-deep margin (whose plane extrapolation gained a real depth advantage)
    // yields the shared ridge to the neighbor face's exact footprint, while a
    // sub-pixel gap-fill yields almost nothing and still wins (#1883).
    const vec2 outside = max(max(-vQuadParam, vQuadParam - vec2(1.0)), vec2(0.0));
    // Interior-edge yield floor (#2428): a margin that penetrated an INTERIOR edge
    // is extending over the adjacent visible face — floor its yield slope at the
    // cross-face divergence bound so it always loses to that face's exact
    // fragments (see vMarginYieldGradFloor). The penetrated side is u/v-low when
    // vQuadParam < 0, u/v-high when > 1; vEdgeInterior packs (u-low, u-high,
    // v-low, v-high).
    const float interiorU = (vQuadParam.x < 0.5) ? vEdgeInterior.x : vEdgeInterior.y;
    const float interiorV = (vQuadParam.y < 0.5) ? vEdgeInterior.z : vEdgeInterior.w;
    const float gradU = (interiorU > 0.5)
        ? max(vMarginYieldGradU, vMarginYieldGradFloor)
        : vMarginYieldGradU;
    const float gradV = (interiorV > 0.5)
        ? max(vMarginYieldGradV, vMarginYieldGradFloor)
        : vMarginYieldGradV;
    // The flat interior term (see vMarginInteriorYieldBias) covers the
    // penetration-INDEPENDENT (flip<<2)|slot key gap between adjacent faces; the
    // floored slope covers the penetration-proportional plane divergence.
    const bool interiorPen =
        (outside.x > 0.0 && interiorU > 0.5) || (outside.y > 0.0 && interiorV > 0.5);
    const float yieldBias = vMarginDepthBias + outside.x * gradU + outside.y * gradV +
        (interiorPen ? vMarginInteriorYieldBias : 0.0);
    // #2255: band-quantize + cell-code injection (see vCellTieOffset above).
    // Exact power-of-two float ops, so same-band fragments from different
    // cells land on bit-distinct, cell-ordered depths on every backend.
    const float scatterDepth = vDepth + (inMargin ? yieldBias : 0.0);
    gl_FragDepth =
        floor(scatterDepth / kScatterCellTieBand) * kScatterCellTieBand + vCellTieOffset;
}

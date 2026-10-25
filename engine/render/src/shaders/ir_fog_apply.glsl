#ifndef IR_FOG_APPLY_GLSL_INCLUDED
#define IR_FOG_APPLY_GLSL_INCLUDED

// Display-space fog composition has no texture or observer-buffer dependency.
#include "ir_fog_color.glsl"

// Cross-section cap: the tint applied to a hidden VERTICAL face within
// kFogCutMaxRimCells of the disc rim (radial surface-XY distance, the metric
// the reveal and rim fade key on). A pure multiply with kFogRimFadeLevel <
// tone < 1, so brightness stays monotone across the rim on every material.
const float kFogCutTone = 0.85;
const float kFogCutMaxRimCells = 2.0;
// Rim fade: an unexplored sample's tone is lifted toward its lit colour,
// starting at kFogRimFadeLevel at the rim and decaying to zero over
// kFogRimFadeCells. kFogRimFadeCells MUST equal kFogHiddenKeepCells
// (ir_voxel_face_select.glsl) so the fade bottoms out exactly where hidden
// columns stop rasterizing. Hard discs only.
const float kFogRimFadeCells = 8.0;
const float kFogRimFadeLevel = 0.75;

struct FogReveal {
    float state;
    // The grid read alone: the rim fade and the cut cap apply only to
    // UNEXPLORED surfaces.
    float gridState;
    // World distance past the nearest hard disc's radius; the full fade width
    // when no hard disc reaches the sample (cap off, fade 0).
    float hardDistPastRim;
    // The reveal-surface cap's blend weight: the band weight of the retained
    // partial contribution that established `state`, 0 when the sample is
    // untreated (treatment off, the grid or a full source decides it, or its
    // winning voxel dissolved).
    float styledBand;
};

// The cap tone's blend weight across a partial cut: 0 where the surface
// factor is 0 or 1, 1 at the midpoint, so the cap meets the untreated colour
// at both edges of the band.
float fogRevealSurfaceBandWeight(float surfaceVisibility) {
    return 4.0 * surfaceVisibility * (1.0 - surfaceVisibility);
}

// The cut-cap blend the radial rim cap and the reveal-surface cap share:
// `color` pulled by `weight` toward the source colour toned by `tone` at
// state 0 and untoned at state 1.
vec3 fogCutCapBlend(vec3 color, vec3 sourceColor, float tone, float state, float weight) {
    return mix(color, mix(sourceColor * tone, sourceColor, state), weight);
}

// Only meaningful for state < 1.0; a fully revealed sample keeps its colour.
vec4 fogApplyReveal(FogReveal reveal, int faceAxis, vec4 sourceColor, vec4 unexploredColor,
    int visionCircleCount, vec4 revealSurfaceTreatment) {
    vec3 outColor = fogStateColor(reveal.state, sourceColor.rgb, unexploredColor.rgb);
    if (reveal.gridState < kFogExploredValue) {
        // The squared ease-out crushes the fade tail well before the keep-ring
        // drop, so the outermost kept wall faces can't catch a visible lift.
        const float u = 1.0 - smoothstep(0.0, kFogRimFadeCells, reveal.hardDistPastRim);
        outColor = mix(outColor, sourceColor.rgb, kFogRimFadeLevel * u * u);
        // TOP (Z) faces are never capped; VERTICAL faces converge to the plain
        // fade exactly at kFogCutMaxRimCells.
        if (visionCircleCount > 0 && faceAxis != 2) {
            const float capBlend =
                1.0 - smoothstep(0.0, kFogCutMaxRimCells, max(reveal.hardDistPastRim, 0.0));
            outColor = fogCutCapBlend(outColor, sourceColor.rgb, kFogCutTone, reveal.state, capBlend);
        }
    }
    // The reveal-surface cap marks a retained partial cut on every face axis,
    // in the canvas's tone; the ceiling and line-of-sight cuts share it.
    if (reveal.styledBand > 0.0) {
        outColor = fogCutCapBlend(
            outColor, sourceColor.rgb, revealSurfaceTreatment.z, reveal.state, reveal.styledBand
        );
    }
    return vec4(outColor, sourceColor.a);
}

// A BODY sample takes its body's one verdict: `state` is the carrier factor,
// with no grid tap, height term, line-of-sight gate, rim fade or cut cap, so
// the whole body reads at one tone. Only meaningful for state < 1.0.
vec4 fogApplyBody(float state, vec4 sourceColor, vec4 unexploredColor) {
    return vec4(fogStateColor(state, sourceColor.rgb, unexploredColor.rgb), sourceColor.a);
}

#endif

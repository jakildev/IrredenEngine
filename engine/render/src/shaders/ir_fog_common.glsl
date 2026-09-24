// Per-sample fog shading shared by every FOG_TO_TRIXEL route: the main canvas,
// the per-axis canvases, and the per-axis overflow lane. Each sample's
// world-space pos3D is masked by the MAX of two reveal sources:
//   1. The voxel GRID — a single NEAREST read of the fog texture at the
//      rounded cell. Coarse, voxel-quantized: explored/voxelized memory.
//   2. Live analytic VISION CIRCLES (FogObserverData) evaluated against the
//      CONTINUOUS world column, so a disc edge is crisp and slides smoothly
//      with sub-voxel observer motion.
// The reveal is split from the colour apply so a caller can skip the colour
// read-modify-write for a fully revealed sample (state >= 1.0), which is most
// of a revealed scene's pixels.
//
// Include-FRAGMENT: the wrapper defines IR_FOG_LOS_BINDING before including it
// (ir_fog_los). Metal twin: metal/ir_fog_common.metal — the reveal loop and the
// apply body must normalize equal (FogCrossSectionShaderParity).

#include "ir_iso_common.glsl"
#include "ir_fog_los.glsl"

// Mirrors C_CanvasFogOfWar: the world-space fog grid is centered on origin.
const int kFogOfWarHalfExtent = 128;
// Normalized stored explored value (128/255, NOT 0.5). The two-segment lerp
// pivots through it so the canonical stored states 0 / 128 / 255 land exactly
// on the unexplored / explored / source anchors.
const float kFogExploredValue = 128.0 / 255.0;
// Mirrors kMaxFogVisionCircles in component_canvas_fog_of_war.hpp.
const int kMaxFogVisionCircles = 8;
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

// binding 27 ALIASES kBufferIndex_FrameDataLightingToTrixel — the Metal 0-30
// buffer table is full, and fog runs after lighting is done with the slot.
layout(std140, binding = 27) uniform FogObserverData {
    // (centerX, centerY, radius, edgeSoftness) in world units.
    vec4 visionCircles[kMaxFogVisionCircles];
    int visionCircleCount;
    // Bit i set = source i is gated by the line-of-sight field (ir_fog_los).
    int losSourceMask;
    // (observerZ, zCostUp, zCostDown, freeBand); all-zero = the plain 2D disc.
    vec4 visionCircleHeights[kMaxFogVisionCircles];
    // The lerp's state-0 anchor.
    vec4 unexploredColor;
};

layout(rgba8, binding = 2) readonly uniform image2D canvasFogOfWar;

struct FogReveal {
    float state;
    // The grid read alone: the rim fade and the cut cap apply only to
    // UNEXPLORED surfaces.
    float gridState;
    // World distance past the nearest hard disc's radius; the full fade width
    // when no hard disc reaches the sample (cap off, fade 0).
    float hardDistPastRim;
};

// Out-of-range cells read as visible (1.0): imageLoad has no sampler wrap
// mode, so this bounds check is load-bearing.
float fogTap(ivec2 cell, ivec2 fogSize) {
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0;
    }
    return imageLoad(canvasFogOfWar, cell).r;
}

vec3 fogStateColor(float state, vec3 sourceColor, vec3 unexplored) {
    const float luminance = dot(sourceColor, vec3(0.299, 0.587, 0.114));
    const vec3 exploredColor = vec3(luminance) * 0.4;
    if (state >= kFogExploredValue) {
        const float t = (state - kFogExploredValue) / (1.0 - kFogExploredValue);
        return mix(exploredColor, sourceColor, t);
    }
    const float t = state / kFogExploredValue;
    return mix(unexplored, exploredColor, t);
}

// `aaFloor` (world units per canvas pixel) and `fogWholeBody` are read only
// by the vision-circle loop, so callers may skip computing them when
// visionCircleCount is 0.
FogReveal fogRevealSample(vec3 pos3D, float aaFloor, bool fogWholeBody) {
    const ivec3 surfaceVoxel = roundHalfUp(pos3D);
    const ivec2 fogCell = surfaceVoxel.xy + ivec2(kFogOfWarHalfExtent);
    const float gridState = fogTap(fogCell, imageSize(canvasFogOfWar));
    float state = gridState;
    float hardDistPastRim = kFogRimFadeCells;

    for (int i = 0; i < visionCircleCount; ++i) {
        // An occluded source contributes neither reveal nor rim distance. A
        // whole-body pixel's visibility is its anchor's verdict, so it is
        // never gated per pixel.
        if (fogLosSourceGated(losSourceMask, i) && !fogWholeBody &&
            !fogLosVisible(surfaceVoxel, i)) {
            continue;
        }
        // Height-penalized reveal; a whole-body pixel drops both terms.
        const vec4 heights = visionCircleHeights[i];
        const float zCostUp = fogWholeBody ? 0.0 : heights.y;
        const float zCostDown = fogWholeBody ? 0.0 : heights.z;
        const float dzUp = max(heights.x - pos3D.z, 0.0);
        const float dzDown = max(pos3D.z - heights.x, 0.0);
        const float distEff = length(pos3D.xy - visionCircles[i].xy) +
            zCostUp * max(dzUp - heights.w, 0.0) +
            zCostDown * max(dzDown - heights.w, 0.0);
        const float aa = max(visionCircles[i].w, aaFloor);
        const float reveal =
            1.0 - smoothstep(visionCircles[i].z - aa, visionCircles[i].z + aa, distEff);
        state = max(state, reveal);
        if (visionCircles[i].w == 0.0) {
            hardDistPastRim = min(hardDistPastRim, distEff - visionCircles[i].z);
        }
    }
    return FogReveal(state, gridState, hardDistPastRim);
}

// Only meaningful for state < 1.0; a fully revealed sample keeps its colour.
vec4 fogApplyReveal(FogReveal reveal, int faceAxis, vec4 sourceColor) {
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
            const vec3 capColor = mix(sourceColor.rgb * kFogCutTone, sourceColor.rgb, reveal.state);
            outColor = mix(outColor, capColor, capBlend);
        }
    }
    return vec4(outColor, sourceColor.a);
}

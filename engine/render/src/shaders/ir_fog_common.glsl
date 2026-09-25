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
    // The lerp's state-0 anchor. Only the fog passes declare it and the tail
    // below; every other declaration of the block stops earlier.
    vec4 unexploredColor;
    // Per-source line-of-sight softness, source i at [i / 4][i % 4]: < 0 is the
    // hard gate (fogLosVisible), >= 0 the smooth gate with that band in voxels.
    vec4 losSoftness[2];
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

// True when a source reads the smooth line-of-sight gate for this sample, so a
// caller builds its FogLosSample only then.
bool fogLosSmoothSampleNeeded(bool fogWholeBody) {
    if (fogWholeBody) {
        return false;
    }
    for (int i = 0; i < visionCircleCount; ++i) {
        if (fogLosSourceGated(losSourceMask, i) && losSoftness[i >> 2][i & 3] >= 0.0) {
            return true;
        }
    }
    return false;
}

// The smooth line-of-sight sample of a sample whose pos3D rounds to the voxel
// that emitted it — a per-axis cell or overflow entry, whose hard gate reads
// that same voxel: a top face samples its position, a vertical face the column
// that voxel's face looks into (ir_fog_los).
FogLosSample fogLosVoxelSample(vec3 pos3D, int faceId) {
    if ((faceId >> 1) == kZFace) {
        return fogLosSurfaceSample(pos3D);
    }
    return fogLosFaceSample(roundHalfUp(pos3D), faceId);
}

// `aaFloor` (world units per canvas pixel), `fogWholeBody` and `losSample` are
// read only by the vision-circle loop, so callers may skip computing them when
// visionCircleCount is 0; `losSample` is read only when
// fogLosSmoothSampleNeeded.
FogReveal fogRevealSample(vec3 pos3D, float aaFloor, bool fogWholeBody, FogLosSample losSample) {
    const ivec3 surfaceVoxel = roundHalfUp(pos3D);
    const ivec2 fogCell = surfaceVoxel.xy + ivec2(kFogOfWarHalfExtent);
    const float gridState = fogTap(fogCell, imageSize(canvasFogOfWar));
    float state = gridState;
    float hardDistPastRim = kFogRimFadeCells;
    // The smooth gate's taps load on first use, one tap set per four-source
    // tile.
    FogLosTaps losTaps0;
    FogLosTaps losTaps1;
    bool losTapsLoaded0 = false;
    bool losTapsLoaded1 = false;

    for (int i = 0; i < visionCircleCount; ++i) {
        // An occluded source contributes neither reveal nor rim distance; a
        // smooth source scales both by its visibility. A whole-body pixel's
        // visibility is its anchor's verdict, so it is never gated per pixel.
        float losVisibility = 1.0;
        const float softness = losSoftness[i >> 2][i & 3];
        if (fogLosSourceGated(losSourceMask, i) && !fogWholeBody && softness < 0.0 &&
            !fogLosVisible(surfaceVoxel, i)) {
            continue;
        }
        if (fogLosSourceGated(losSourceMask, i) && !fogWholeBody && softness >= 0.0) {
            if (i < kFogLosSourcesPerTile) {
                if (!losTapsLoaded0) {
                    losTaps0 = fogLosLoadTaps(losSample, 0);
                    losTapsLoaded0 = true;
                }
                losVisibility = fogLosSmoothVisibility(losTaps0, i, losSample, softness);
            } else {
                if (!losTapsLoaded1) {
                    losTaps1 = fogLosLoadTaps(losSample, 1);
                    losTapsLoaded1 = true;
                }
                losVisibility = fogLosSmoothVisibility(losTaps1, i, losSample, softness);
            }
            if (losVisibility <= 0.0) {
                continue;
            }
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
        const float reveal = losVisibility *
            (1.0 - smoothstep(visionCircles[i].z - aa, visionCircles[i].z + aa, distEff));
        state = max(state, reveal);
        if (visionCircles[i].w == 0.0) {
            // A partly visible smooth source eases the rim distance toward no
            // lift, so the rim lift and cut cap fade across the band with the
            // reveal.
            const float distPastRim = distEff - visionCircles[i].z;
            hardDistPastRim = min(
                hardDistPastRim,
                losVisibility < 1.0 ? mix(kFogRimFadeCells, distPastRim, losVisibility)
                                    : distPastRim
            );
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

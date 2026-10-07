// Per-sample fog shading shared by every FOG_TO_TRIXEL route: the main canvas,
// the per-axis canvases, and the per-axis overflow lane. Each sample's
// world-space pos3D is masked by the MAX of two reveal sources:
//   1. The voxel GRID — a single NEAREST read of the fog texture at the
//      rounded cell. Coarse, voxel-quantized: explored/voxelized memory.
//   2. Live analytic VISION CIRCLES (FogObserverData) evaluated against the
//      CONTINUOUS world column, so a disc edge is crisp and slides smoothly
//      with sub-voxel observer motion. A line-of-sight gated source is scaled
//      by the exact segment march (ir_fog_los) from its eye to the sample's
//      canonical position, which each route builds with
//      fogLosCanonicalSample.
// The reveal is split from the colour apply so a caller can skip the colour
// read-modify-write for a fully revealed sample (state >= 1.0), which is most
// of a revealed scene's pixels. The reveal relies on that early-out: it stops
// evaluating sources once state reaches 1.0, and it skips a gated source's
// march whenever the march cannot change what the caller writes, so only a
// march that can move the output is paid for.
//
// Include-FRAGMENT: the wrapper defines IR_FOG_LOS_BINDING before including it
// (ir_fog_los). Metal twin: metal/ir_fog_common.metal — the reveal loop and the
// apply body must normalize equal (FogCrossSectionShaderParity).

#include "ir_iso_common.glsl"
#include "ir_fog_color.glsl"
#include "ir_fog_los.glsl"

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
    // The field column at texel (0, 0) of the fog window (C_CanvasFogOfWar's
    // windowOrigin_), uploaded in the same frame as the texture it indexes.
    int windowOriginX;
    int windowOriginY;
    // (observerZ, zCostUp, zCostDown, freeBand); all-zero = the plain 2D disc.
    vec4 visionCircleHeights[kMaxFogVisionCircles];
    // The lerp's state-0 anchor. Only the fog passes declare it and the tail
    // below; every other declaration of the block stops earlier.
    vec4 unexploredColor;
    // Per-source line of sight, (eye height above observerZ, softness, 0, 0);
    // read only for sources in losSourceMask.
    vec4 losParams[kMaxFogVisionCircles];
    // Source reveal masks packed four per std140 vector. A source reveals a
    // FIELD sample only where its mask intersects the sampled cell's mask
    // (the window's .g lane).
    uvec4 visionCircleChannels[2];
    // Per-source upward ceiling, (ceilingHeight, fadeHeight, 0, 0) in world
    // units above observerZ; a negative height is off (fogCeilingVisibility).
    vec4 visionCircleCeilings[kMaxFogVisionCircles];
    // The canvas's reveal-surface treatment, (enabled, dissolveDensity,
    // capTone, 0): with x non-zero a partial ceiling or line-of-sight cut on
    // a FIELD sample is dissolved per world voxel and cap-toned.
    vec4 revealSurfaceTreatment;
};

layout(rg32ui, binding = 2) readonly uniform uimage2D canvasFogOfWar;

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

// The per-voxel dissolve key in [0, 1): fixed-width integer xor / multiply
// steps over the sample's integer world voxel and nothing else, so every
// route, yaw, frame and backend decides one way per voxel. CPU twin:
// IRPrefab::Fog::detail::revealSurfaceHash01.
float fogRevealSurfaceHash01(ivec3 voxel) {
    uint hash = uint(voxel.x) * 0x8DA6B343u ^ uint(voxel.y) * 0xD8163841u ^
        uint(voxel.z) * 0xCB1AB31Fu;
    hash ^= hash >> 16u;
    hash *= 0x7FEB352Du;
    hash ^= hash >> 15u;
    hash *= 0x846CA68Bu;
    hash ^= hash >> 16u;
    return float(hash & 0xFFFFFFu) / 16777216.0;
}

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

// Texel of world column `col` in the fog window, or (-1, -1) when the column
// is outside it. Column `c` lives at texel floorMod(c, W) with W the window
// edge (imageSize); the window covers [origin, origin + W) per axis. Every
// modulo takes non-negative operands only (GLSL leaves the negative case
// undefined). Mirrors fogWindowTexel in ir_voxel_face_select.glsl; Metal
// twin in metal/ir_fog_common.metal.
ivec2 fogWindowTexel(ivec2 col, ivec2 origin, ivec2 fogSize) {
    const ivec2 rel = col - origin;
    if (rel.x < 0 || rel.x >= fogSize.x || rel.y < 0 || rel.y >= fogSize.y) {
        return ivec2(-1);
    }
    ivec2 base;
    base.x = origin.x >= 0 ? origin.x % fogSize.x : fogSize.x - 1 - (-(origin.x + 1)) % fogSize.x;
    base.y = origin.y >= 0 ? origin.y % fogSize.y : fogSize.y - 1 - (-(origin.y + 1)) % fogSize.y;
    ivec2 texel = rel + base;
    if (texel.x >= fogSize.x) {
        texel.x -= fogSize.x;
    }
    if (texel.y >= fogSize.y) {
        texel.y -= fogSize.y;
    }
    return texel;
}

// Grid texel of world column `col`: (state integer, channel mask), or
// (0, kFogChannelDefault) — unexplored on the default channel — for a column
// outside the window. imageLoad has no sampler wrap mode, so the window test
// is load-bearing; the vision circles still max-compose over an out-of-window
// column.
uvec2 fogTapTexel(ivec2 col, ivec2 fogSize) {
    const ivec2 cell = fogWindowTexel(col, ivec2(windowOriginX, windowOriginY), fogSize);
    if (cell.x < 0) {
        return uvec2(0u, kFogChannelDefault);
    }
    return imageLoad(canvasFogOfWar, cell).rg;
}

// The FIELD reveal. `aaFloor` (world units per canvas pixel) and `losSample`
// (the sample's canonical position, fogLosCanonicalSample) are read only by
// the vision-circle loop, so callers may skip computing them when
// visionCircleCount is 0; `losSample` is read only for a gated source. A BODY
// sample never reaches it (fogApplyBody).
FogReveal fogRevealSample(vec3 pos3D, vec3 losSample, float aaFloor) {
    const ivec3 surfaceVoxel = roundHalfUp(pos3D);
    const ivec2 fogSize = imageSize(canvasFogOfWar);
    const uvec2 gridTexel = fogTapTexel(surfaceVoxel.xy, fogSize);
    const float gridState = fogTexelState(gridTexel.x);
    const uint cellChannels = gridTexel.y;
    // The line-of-sight field is anchored with the window this texture shows.
    const ivec2 losFieldMin = fogLosFieldMin(ivec2(windowOriginX, windowOriginY), fogSize.x);
    float state = gridState;
    float hardDistPastRim = kFogRimFadeCells;
    float styledBand = 0.0;
    const bool treated = revealSurfaceTreatment.x != 0.0;
    float hash01 = -1.0;

    for (int i = 0; i < visionCircleCount; ++i) {
        // Every caller returns without reading the reveal at state >= 1.0.
        if (state >= 1.0) {
            break;
        }
        if ((visionCircleChannels[i / 4][i % 4] & cellChannels) == 0u) {
            continue;
        }
        // Height-penalized reveal.
        const vec4 heights = visionCircleHeights[i];
        const float dzUp = max(heights.x - pos3D.z, 0.0);
        const float dzDown = max(pos3D.z - heights.x, 0.0);
        const float distEff = length(pos3D.xy - visionCircles[i].xy) +
            heights.y * max(dzUp - heights.w, 0.0) +
            heights.z * max(dzDown - heights.w, 0.0);
        const float aa = max(visionCircles[i].w, aaFloor);
        const float reveal =
            1.0 - smoothstep(visionCircles[i].z - aa, visionCircles[i].z + aa, distEff);
        const float distPastRim = distEff - visionCircles[i].z;
        // The ceiling scales this source alone, before the maximum; an off
        // ceiling is exactly 1.0, so every value below stays bit-identical.
        const float ceilingVisibility = fogCeilingVisibility(visionCircleCeilings[i], dzUp);
        const float ceilingReveal = reveal * ceilingVisibility;
        // Exact skip: a gated source adds at most `ceilingReveal` to the max,
        // and its rim distance is dead when the disc is soft (soft discs
        // never feed it) or the grid is at least explored (fogApplyReveal
        // reads it only below). Skipping leaves every value the caller reads
        // bit-identical.
        if (fogLosSourceGated(losSourceMask, i) && ceilingReveal <= state &&
            (visionCircles[i].w != 0.0 || gridState >= kFogExploredValue)) {
            continue;
        }
        // A gated source is scaled by its line of sight to the sample; only a
        // sample the source can reveal or rim-lift is marched.
        float losVisibility = 1.0;
        if (fogLosSourceGated(losSourceMask, i) &&
            (ceilingReveal > 0.0 ||
             (visionCircles[i].w == 0.0 && distPastRim < kFogRimFadeCells)) &&
            length(losSample.xy - visionCircles[i].xy) <= fogLosReach(visionCircles[i])) {
            losVisibility = fogLosVisibility(
                fogLosEye(visionCircles[i], heights.x, losParams[i].x),
                losSample,
                losParams[i].y,
                losFieldMin
            );
        }
        // An occluded source contributes neither reveal nor rim distance; a
        // partly visible one scales both, easing its rim distance toward no
        // lift so the rim lift and cut cap fade with the reveal.
        if (losVisibility <= 0.0) {
            continue;
        }
        const float contribution = losVisibility * ceilingReveal;
        // A partial surface cut (ceiling and line of sight multiplied) is
        // dissolved per world voxel: a rejected voxel contributes nothing,
        // so a dissolved source never lowers what the grid or another source
        // reveals, and the retained ones carry the cap's band weight.
        float bandWeight = 0.0;
        if (treated && contribution > 0.0) {
            const float surfaceVisibility = ceilingVisibility * losVisibility;
            if (surfaceVisibility < 1.0) {
                if (hash01 < 0.0) {
                    hash01 = fogRevealSurfaceHash01(surfaceVoxel);
                }
                if (hash01 > mix(1.0, surfaceVisibility, revealSurfaceTreatment.y)) {
                    continue;
                }
                bandWeight = fogRevealSurfaceBandWeight(surfaceVisibility);
            }
        }
        // The band follows the contribution that establishes the state; a
        // tie keeps the larger band.
        if (contribution > state) {
            state = contribution;
            styledBand = bandWeight;
        } else if (contribution == state && bandWeight > styledBand) {
            styledBand = bandWeight;
        }
        if (visionCircles[i].w == 0.0) {
            hardDistPastRim = min(
                hardDistPastRim,
                losVisibility < 1.0 ? mix(kFogRimFadeCells, distPastRim, losVisibility) : distPastRim
            );
        }
    }
    return FogReveal(state, gridState, hardDistPastRim, styledBand);
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
vec4 fogApplyBody(float state, vec4 sourceColor) {
    return vec4(fogStateColor(state, sourceColor.rgb, unexploredColor.rgb), sourceColor.a);
}

// The BODY state an overflow entry's class byte carries (encodeFogOverflowClassByte).
float fogOverflowBodyState(uint classByte) {
    return float(classByte) / 254.0;
}

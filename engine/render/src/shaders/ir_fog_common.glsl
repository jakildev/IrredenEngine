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
// A sample on a vertical face also takes the reveal at the top plane of its
// own column (fogColumnTopState), on frames with a gated source.
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
#include "ir_fog_apply.glsl"
#include "ir_fog_los.glsl"

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
    // capTone, phase): with x non-zero a partial ceiling or line-of-sight cut on
    // a FIELD sample is dissolved per world voxel and cap-toned.
    vec4 revealSurfaceTreatment;
};

layout(rg32ui, binding = 2) readonly uniform uimage2D canvasFogOfWar;

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

bool fogCeilingCutFace(vec3 facePoint, int faceId) {
    const ivec3 normal = faceOutwardNormal6I(faceId);
    const ivec3 owner = roundHalfUp(facePoint - 0.5 * vec3(normal));
    const ivec3 neighbor = owner + normal;
    const uvec2 cell = fogTapTexel(neighbor.xy, imageSize(canvasFogOfWar));
    return !fogCeilingRetained(neighbor, cell, visionCircles,
        visionCircleHeights, visionCircleCeilings, visionCircleChannels,
        visionCircleCount, revealSurfaceTreatment, revealSurfaceTreatment.w);
}

// The column-top term of a vertical face: the reveal at the top plane of the
// column `columnPoint` lies in, the ground surface the face's matter stands
// under. Each admitted source contributes its radial curve, ceiling and line
// of sight evaluated at the point of that plane over `columnPoint`, which is
// the verdict a body standing there takes. Returns the largest of `state` and
// those contributions; a column with no top plane above the sample returns
// `state`. The term feeds the state alone, so a source whose curve at the
// plane cannot exceed it is skipped whole, march included, and the result is
// bit-identical to evaluating it.
float fogColumnTopState(
    vec3 columnPoint, float aaFloor, uint cellChannels, ivec2 losFieldMin, float state
) {
    const float topZ = fogLosTopPlane(fogLosHalfCellOf(columnPoint.xy), losFieldMin);
    if (topZ == kFogLosColumnEmpty || topZ >= columnPoint.z - kFogLosClearanceTolerance) {
        return state;
    }
    const vec3 top = vec3(columnPoint.xy, topZ);
    for (int i = 0; i < visionCircleCount; ++i) {
        if ((visionCircleChannels[i / 4][i % 4] & cellChannels) == 0u) {
            continue;
        }
        const vec4 heights = visionCircleHeights[i];
        const float dzUp = max(heights.x - top.z, 0.0);
        const float dzDown = max(top.z - heights.x, 0.0);
        const float distEff = length(top.xy - visionCircles[i].xy) +
            heights.y * max(dzUp - heights.w, 0.0) +
            heights.z * max(dzDown - heights.w, 0.0);
        const float aa = max(visionCircles[i].w, aaFloor);
        const float ceilingReveal =
            (1.0 - smoothstep(visionCircles[i].z - aa, visionCircles[i].z + aa, distEff)) *
            fogCeilingVisibility(visionCircleCeilings[i], dzUp);
        if (ceilingReveal <= state) {
            continue;
        }
        float losVisibility = 1.0;
        if (fogLosSourceGated(losSourceMask, i) &&
            length(top.xy - visionCircles[i].xy) <= fogLosReach(visionCircles[i])) {
            losVisibility = fogLosVisibility(
                fogLosEye(visionCircles[i], heights.x, losParams[i].x),
                top,
                losParams[i].y,
                losFieldMin
            );
        }
        state = max(state, losVisibility * ceilingReveal);
    }
    return state;
}

// The FIELD reveal. `aaFloor` (world units per canvas pixel) and `losSample`
// (the sample's canonical position, fogLosCanonicalSample) are read only by
// the vision-circle loop, so callers may skip computing them when
// visionCircleCount is 0; `losSample` is read only for a gated source.
// `columnPoint` (fogLosColumnPoint) and `faceAxis` (the face's world axis, 2 =
// a top or bottom face) are read only while a source is gated, the frames the
// column field is published on. A BODY sample never reaches it (fogApplyBody).
FogReveal fogRevealSample(
    vec3 pos3D, vec3 losSample, vec3 columnPoint, int faceAxis, float aaFloor,
    bool analyticSurface
) {
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
        // Voxel ceiling dissolve already selected geometry. Analytic surfaces
        // keep the composed ceiling/LOS dissolve; both routes retain its band.
        float bandWeight = 0.0;
        if (treated && contribution > 0.0) {
            const float surfaceVisibility = ceilingVisibility * losVisibility;
            const float dissolveVisibility = analyticSurface ? surfaceVisibility : losVisibility;
            if (dissolveVisibility < 1.0) {
                if (hash01 < 0.0) {
                    hash01 = fogRevealSurfaceHash01(surfaceVoxel);
                }
                if (hash01 > mix(1.0, dissolveVisibility, revealSurfaceTreatment.y)) {
                    continue;
                }
            }
            bandWeight = fogRevealSurfaceBandWeight(surfaceVisibility);
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
    // A vertical face below a revealed top plane of its own column is
    // revealed with it. A top face is its own ground surface and never takes
    // the term: a height test alone would light the foot of a shadowed wall
    // wherever a top pixel's recovered XY lands inside the taller neighbour.
    // A sample the term raises carries no band, so no dissolve or cap marks
    // it; the rim distance and the grid state stay the sample's own.
    if (state < 1.0 && losSourceMask != 0 && faceAxis != 2) {
        const float topState =
            fogColumnTopState(columnPoint, aaFloor, cellChannels, losFieldMin, state);
        if (topState > state) {
            state = topState;
            styledBand = 0.0;
        }
    }
    return FogReveal(state, gridState, hardDistPastRim, styledBand);
}

vec4 fogApplyReveal(FogReveal reveal, int faceAxis, vec4 sourceColor) {
    return fogApplyReveal(reveal, faceAxis, sourceColor, unexploredColor,
        visionCircleCount, revealSurfaceTreatment);
}

vec4 fogApplyBody(float state, vec4 sourceColor) {
    return fogApplyBody(state, sourceColor, unexploredColor);
}

// The BODY state an overflow entry's class byte carries (encodeFogOverflowClassByte).
float fogOverflowBodyState(uint classByte) {
    return float(classByte) / 254.0;
}

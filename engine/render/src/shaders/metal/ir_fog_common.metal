#ifndef IR_FOG_COMMON_METAL_INCLUDED
#define IR_FOG_COMMON_METAL_INCLUDED

// Metal twin of ../ir_fog_common.glsl — the reveal model, constants, and the
// reveal / apply split are documented there. The reveal loop and the apply
// body must normalize equal to the GLSL twin (FogCrossSectionShaderParity).

#include "ir_iso_common.metal"
#include "ir_fog_apply.metal"
#include "ir_fog_los.metal"

struct FogObserverData {
    float4 visionCircles[kMaxFogVisionCircles];
    int visionCircleCount;
    // Bit i set = source i is gated by the line-of-sight field (ir_fog_los).
    int losSourceMask;
    // The field column at texel (0, 0) of the fog window (C_CanvasFogOfWar's
    // windowOrigin_), uploaded in the same frame as the texture it indexes.
    int windowOriginX;
    int windowOriginY;
    float4 visionCircleHeights[kMaxFogVisionCircles];
    float4 unexploredColor;
    // Per-source line of sight, (eye height above observerZ, softness, 0, 0).
    float4 losParams[kMaxFogVisionCircles];
    uint4 visionCircleChannels[2];
    // Per-source upward ceiling, (ceilingHeight, fadeHeight, 0, 0); negative = off.
    float4 visionCircleCeilings[kMaxFogVisionCircles];
    // The canvas's reveal-surface treatment, (enabled, dissolveDensity, capTone, phase).
    float4 revealSurfaceTreatment;
};

// Grid texel of world column `col`: (state integer, channel mask), or
// (0, kFogChannelDefault) for a column outside the window. GLSL twin:
// fogTapTexel in ../ir_fog_common.glsl.
inline uint2 fogTapTexel(
    int2 col,
    int2 origin,
    int2 fogSize,
    texture2d<uint, access::read> canvasFogOfWar
) {
    const int2 cell = fogWindowTexel(col, origin, fogSize);
    if (cell.x < 0) {
        return uint2(0u, kFogChannelDefault);
    }
    return canvasFogOfWar.read(uint2(cell)).rg;
}

inline bool fogCeilingCutFace(
    float3 facePoint, int faceId, constant FogObserverData& fogObservers,
    texture2d<uint, access::read> canvasFogOfWar
) {
    const int3 normal = faceOutwardNormal6I(faceId);
    const int3 owner = roundHalfUp(facePoint - 0.5f * float3(normal));
    const int3 neighbor = owner + normal;
    const int2 fogSize = int2(int(canvasFogOfWar.get_width()), int(canvasFogOfWar.get_height()));
    const uint2 cell = fogTapTexel(neighbor.xy,
        int2(fogObservers.windowOriginX, fogObservers.windowOriginY), fogSize, canvasFogOfWar);
    return !fogCeilingRetained(neighbor, cell, fogObservers.visionCircles,
        fogObservers.visionCircleHeights, fogObservers.visionCircleCeilings,
        fogObservers.visionCircleChannels, fogObservers.visionCircleCount,
        fogObservers.revealSurfaceTreatment, fogObservers.revealSurfaceTreatment.w);
}

// GLSL twin: fogColumnTopState in ../ir_fog_common.glsl.
inline float fogColumnTopState(
    float3 columnPoint,
    float aaFloor,
    uint cellChannels,
    int2 losFieldMin,
    float state,
    constant FogObserverData& fogObservers,
    texture2d<float, access::read> fogLineOfSight
) {
    const float topZ =
        fogLosTopPlane(fogLosHalfCellOf(columnPoint.xy), losFieldMin, fogLineOfSight);
    if (topZ == kFogLosColumnEmpty || topZ >= columnPoint.z - kFogLosClearanceTolerance) {
        return state;
    }
    const float3 top = float3(columnPoint.xy, topZ);
    for (int i = 0; i < fogObservers.visionCircleCount; ++i) {
        if ((fogObservers.visionCircleChannels[i / 4][i % 4] & cellChannels) == 0u) {
            continue;
        }
        const float4 heights = fogObservers.visionCircleHeights[i];
        const float dzUp = max(heights.x - top.z, 0.0f);
        const float dzDown = max(top.z - heights.x, 0.0f);
        const float distEff = length(top.xy - fogObservers.visionCircles[i].xy) +
            heights.y * max(dzUp - heights.w, 0.0f) +
            heights.z * max(dzDown - heights.w, 0.0f);
        const float aa = max(fogObservers.visionCircles[i].w, aaFloor);
        const float ceilingReveal =
            (1.0f - smoothstep(
                fogObservers.visionCircles[i].z - aa,
                fogObservers.visionCircles[i].z + aa,
                distEff
            )) *
            fogCeilingVisibility(fogObservers.visionCircleCeilings[i], dzUp);
        if (ceilingReveal <= state) {
            continue;
        }
        float losVisibility = 1.0f;
        if (fogLosSourceGated(fogObservers.losSourceMask, i) &&
            length(top.xy - fogObservers.visionCircles[i].xy) <=
                fogLosReach(fogObservers.visionCircles[i])) {
            losVisibility = fogLosVisibility(
                fogLosEye(fogObservers.visionCircles[i], heights.x, fogObservers.losParams[i].x),
                top,
                fogObservers.losParams[i].y,
                losFieldMin,
                fogLineOfSight
            );
        }
        state = max(state, losVisibility * ceilingReveal);
    }
    return state;
}

inline FogReveal fogRevealSample(
    float3 pos3D,
    float3 losSample,
    float3 columnPoint,
    int faceAxis,
    float aaFloor,
    bool analyticSurface,
    constant FogObserverData& fogObservers,
    texture2d<uint, access::read> canvasFogOfWar,
    texture2d<float, access::read> fogLineOfSight
) {
    const int3 surfaceVoxel = roundHalfUp(pos3D);
    const int2 fogSize = int2(
        int(canvasFogOfWar.get_width()),
        int(canvasFogOfWar.get_height())
    );
    const uint2 gridTexel = fogTapTexel(
        surfaceVoxel.xy,
        int2(fogObservers.windowOriginX, fogObservers.windowOriginY),
        fogSize,
        canvasFogOfWar
    );
    const float gridState = fogTexelState(gridTexel.x);
    const uint cellChannels = gridTexel.y;
    const int2 losFieldMin = fogLosFieldMin(
        int2(fogObservers.windowOriginX, fogObservers.windowOriginY),
        fogSize.x
    );
    float state = gridState;
    float hardDistPastRim = kFogRimFadeCells;
    float styledBand = 0.0f;
    const bool treated = fogObservers.revealSurfaceTreatment.x != 0.0f;
    float hash01 = -1.0f;

    for (int i = 0; i < fogObservers.visionCircleCount; ++i) {
        if (state >= 1.0f) {
            break;
        }
        if ((fogObservers.visionCircleChannels[i / 4][i % 4] & cellChannels) == 0u) {
            continue;
        }
        const float4 heights = fogObservers.visionCircleHeights[i];
        const float dzUp = max(heights.x - pos3D.z, 0.0f);
        const float dzDown = max(pos3D.z - heights.x, 0.0f);
        const float distEff = length(pos3D.xy - fogObservers.visionCircles[i].xy) +
            heights.y * max(dzUp - heights.w, 0.0f) +
            heights.z * max(dzDown - heights.w, 0.0f);
        const float aa = max(fogObservers.visionCircles[i].w, aaFloor);
        const float reveal = 1.0f - smoothstep(
            fogObservers.visionCircles[i].z - aa,
            fogObservers.visionCircles[i].z + aa,
            distEff
        );
        const float distPastRim = distEff - fogObservers.visionCircles[i].z;
        const float ceilingVisibility =
            fogCeilingVisibility(fogObservers.visionCircleCeilings[i], dzUp);
        const float ceilingReveal = reveal * ceilingVisibility;
        if (fogLosSourceGated(fogObservers.losSourceMask, i) && ceilingReveal <= state &&
            (fogObservers.visionCircles[i].w != 0.0f || gridState >= kFogExploredValue)) {
            continue;
        }
        float losVisibility = 1.0f;
        if (fogLosSourceGated(fogObservers.losSourceMask, i) &&
            (ceilingReveal > 0.0f ||
             (fogObservers.visionCircles[i].w == 0.0f && distPastRim < kFogRimFadeCells)) &&
            length(losSample.xy - fogObservers.visionCircles[i].xy) <=
                fogLosReach(fogObservers.visionCircles[i])) {
            losVisibility = fogLosVisibility(
                fogLosEye(fogObservers.visionCircles[i], heights.x, fogObservers.losParams[i].x),
                losSample,
                fogObservers.losParams[i].y,
                losFieldMin,
                fogLineOfSight
            );
        }
        if (losVisibility <= 0.0f) {
            continue;
        }
        const float contribution = losVisibility * ceilingReveal;
        float bandWeight = 0.0f;
        if (treated && contribution > 0.0f) {
            const float surfaceVisibility = ceilingVisibility * losVisibility;
            const float dissolveVisibility = analyticSurface ? surfaceVisibility : losVisibility;
            if (dissolveVisibility < 1.0f) {
                if (hash01 < 0.0f) {
                    hash01 = fogRevealSurfaceHash01(surfaceVoxel);
                }
                if (hash01 > mix(1.0f, dissolveVisibility, fogObservers.revealSurfaceTreatment.y)) {
                    continue;
                }
            }
            bandWeight = fogRevealSurfaceBandWeight(surfaceVisibility);
        }
        if (contribution > state) {
            state = contribution;
            styledBand = bandWeight;
        } else if (contribution == state && bandWeight > styledBand) {
            styledBand = bandWeight;
        }
        if (fogObservers.visionCircles[i].w == 0.0f) {
            hardDistPastRim = min(
                hardDistPastRim,
                losVisibility < 1.0f ? mix(kFogRimFadeCells, distPastRim, losVisibility)
                                     : distPastRim
            );
        }
    }
    if (state < 1.0f && fogObservers.losSourceMask != 0 && faceAxis != 2) {
        const float topState = fogColumnTopState(
            columnPoint, aaFloor, cellChannels, losFieldMin, state, fogObservers, fogLineOfSight
        );
        if (topState > state) {
            state = topState;
            styledBand = 0.0f;
        }
    }
    return FogReveal{state, gridState, hardDistPastRim, styledBand};
}

inline float4 fogApplyReveal(
    FogReveal reveal, int faceAxis, float4 sourceColor, constant FogObserverData& fogObservers
) {
    return fogApplyReveal(reveal, faceAxis, sourceColor, fogObservers.unexploredColor,
        fogObservers.visionCircleCount, fogObservers.revealSurfaceTreatment);
}

inline float4 fogApplyBody(
    float state, float4 sourceColor, constant FogObserverData& fogObservers
) {
    return fogApplyBody(state, sourceColor, fogObservers.unexploredColor);
}

inline float fogOverflowBodyState(uint classByte) {
    return float(classByte) / 254.0f;
}

// GLSL twin: fogComposeSurface in ../ir_fog_common.glsl.
inline float4 fogComposeSurface(
    float4 litColor, bool body, float bodyState, bool cutFace, float3 pos3D, float3 cutFacePoint,
    int faceId, float aaFloor, bool analyticSurface, constant FogObserverData& fogObservers,
    texture2d<uint, access::read> canvasFogOfWar, texture2d<float, access::read> fogLineOfSight
) {
    if (body) {
        return bodyState < 1.0f ? fogApplyBody(bodyState, litColor, fogObservers) : litColor;
    }
    if (cutFace && fogCeilingCutFace(cutFacePoint, faceId, fogObservers, canvasFogOfWar)) {
        return fogApplyCutCap(litColor, fogObservers.revealSurfaceTreatment.z);
    }
    const FogReveal reveal = fogRevealSample(pos3D, pos3D, pos3D, faceId >> 1, aaFloor,
        analyticSurface, fogObservers, canvasFogOfWar, fogLineOfSight);
    return reveal.state >= 1.0f ? litColor
                                : fogApplyReveal(reveal, faceId >> 1, litColor, fogObservers);
}

#endif // IR_FOG_COMMON_METAL_INCLUDED

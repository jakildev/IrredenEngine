#ifndef IR_FOG_COMMON_METAL_INCLUDED
#define IR_FOG_COMMON_METAL_INCLUDED

// Metal twin of ../ir_fog_common.glsl — the reveal model, constants, and the
// reveal / apply split are documented there. The reveal loop and the apply
// body must normalize equal to the GLSL twin (FogCrossSectionShaderParity).

#include "ir_iso_common.metal"
#include "ir_fog_color.metal"
#include "ir_fog_los.metal"

constant float kFogCutTone = 0.85f;
constant float kFogCutMaxRimCells = 2.0f;
// MUST equal kFogHiddenKeepCells (ir_voxel_face_select.metal).
constant float kFogRimFadeCells = 8.0f;
constant float kFogRimFadeLevel = 0.75f;

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

struct FogReveal {
    float state;
    float gridState;
    float hardDistPastRim;
    float styledBand;
};

inline float fogRevealSurfaceBandWeight(float surfaceVisibility) {
    return 4.0f * surfaceVisibility * (1.0f - surfaceVisibility);
}

inline float3 fogCutCapBlend(
    float3 color, float3 sourceColor, float tone, float state, float weight
) {
    return mix(color, mix(sourceColor * tone, sourceColor, state), weight);
}

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
    FogReveal reveal,
    int faceAxis,
    float4 sourceColor,
    constant FogObserverData& fogObservers
) {
    float3 outColor = fogStateColor(
        reveal.state,
        sourceColor.rgb,
        fogObservers.unexploredColor.rgb
    );
    if (reveal.gridState < kFogExploredValue) {
        const float u = 1.0f - smoothstep(0.0f, kFogRimFadeCells, reveal.hardDistPastRim);
        outColor = mix(outColor, sourceColor.rgb, kFogRimFadeLevel * u * u);
        if (fogObservers.visionCircleCount > 0 && faceAxis != 2) {
            const float capBlend = 1.0f - smoothstep(
                0.0f,
                kFogCutMaxRimCells,
                max(reveal.hardDistPastRim, 0.0f)
            );
            outColor = fogCutCapBlend(outColor, sourceColor.rgb, kFogCutTone, reveal.state, capBlend);
        }
    }
    if (reveal.styledBand > 0.0f) {
        outColor = fogCutCapBlend(
            outColor,
            sourceColor.rgb,
            fogObservers.revealSurfaceTreatment.z,
            reveal.state,
            reveal.styledBand
        );
    }
    return float4(outColor, sourceColor.a);
}

inline float4 fogApplyBody(
    float state,
    float4 sourceColor,
    constant FogObserverData& fogObservers
) {
    return float4(
        fogStateColor(state, sourceColor.rgb, fogObservers.unexploredColor.rgb),
        sourceColor.a
    );
}

inline float fogOverflowBodyState(uint classByte) {
    return float(classByte) / 254.0f;
}

#endif // IR_FOG_COMMON_METAL_INCLUDED

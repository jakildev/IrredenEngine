#ifndef IR_FOG_COMMON_METAL_INCLUDED
#define IR_FOG_COMMON_METAL_INCLUDED

// Metal twin of ../ir_fog_common.glsl — the reveal model, constants, and the
// reveal / apply split are documented there. The reveal loop and the apply
// body must normalize equal to the GLSL twin (FogCrossSectionShaderParity).

#include "ir_iso_common.metal"
#include "ir_fog_color.metal"
#include "ir_fog_los.metal"

constant int kMaxFogVisionCircles = 8;
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
};

struct FogReveal {
    float state;
    float gridState;
    float hardDistPastRim;
};

// Texel of world column `col` in the fog window, or (-1, -1) when the column
// is outside it. GLSL twin: fogWindowTexel in ../ir_fog_common.glsl.
inline int2 fogWindowTexel(int2 col, int2 origin, int2 fogSize) {
    const int2 rel = col - origin;
    if (rel.x < 0 || rel.x >= fogSize.x || rel.y < 0 || rel.y >= fogSize.y) {
        return int2(-1);
    }
    int2 base;
    base.x = origin.x >= 0 ? origin.x % fogSize.x : fogSize.x - 1 - (-(origin.x + 1)) % fogSize.x;
    base.y = origin.y >= 0 ? origin.y % fogSize.y : fogSize.y - 1 - (-(origin.y + 1)) % fogSize.y;
    int2 texel = rel + base;
    if (texel.x >= fogSize.x) {
        texel.x -= fogSize.x;
    }
    if (texel.y >= fogSize.y) {
        texel.y -= fogSize.y;
    }
    return texel;
}

// Grid state of world column `col`: the window texel's .r, or 0.0
// (unexplored) for a column outside the window.
inline float fogTap(
    int2 col,
    int2 origin,
    int2 fogSize,
    texture2d<float, access::read> canvasFogOfWar
) {
    const int2 cell = fogWindowTexel(col, origin, fogSize);
    if (cell.x < 0) {
        return 0.0f;
    }
    return canvasFogOfWar.read(uint2(cell)).r;
}

inline FogReveal fogRevealSample(
    float3 pos3D,
    float3 losSample,
    float aaFloor,
    constant FogObserverData& fogObservers,
    texture2d<float, access::read> canvasFogOfWar,
    texture2d<float, access::read> fogLineOfSight
) {
    const int3 surfaceVoxel = roundHalfUp(pos3D);
    const int2 fogSize = int2(
        int(canvasFogOfWar.get_width()),
        int(canvasFogOfWar.get_height())
    );
    const float gridState = fogTap(
        surfaceVoxel.xy,
        int2(fogObservers.windowOriginX, fogObservers.windowOriginY),
        fogSize,
        canvasFogOfWar
    );
    const int2 losFieldMin = fogLosFieldMin(
        int2(fogObservers.windowOriginX, fogObservers.windowOriginY),
        fogSize.x
    );
    float state = gridState;
    float hardDistPastRim = kFogRimFadeCells;

    for (int i = 0; i < fogObservers.visionCircleCount; ++i) {
        if (state >= 1.0f) {
            break;
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
        if (fogLosSourceGated(fogObservers.losSourceMask, i) && reveal <= state &&
            (fogObservers.visionCircles[i].w != 0.0f || gridState >= kFogExploredValue)) {
            continue;
        }
        float losVisibility = 1.0f;
        if (fogLosSourceGated(fogObservers.losSourceMask, i) &&
            (reveal > 0.0f ||
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
        state = max(state, losVisibility * reveal);
        if (fogObservers.visionCircles[i].w == 0.0f) {
            hardDistPastRim = min(
                hardDistPastRim,
                losVisibility < 1.0f ? mix(kFogRimFadeCells, distPastRim, losVisibility)
                                     : distPastRim
            );
        }
    }
    return FogReveal{state, gridState, hardDistPastRim};
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
            const float3 capColor =
                mix(sourceColor.rgb * kFogCutTone, sourceColor.rgb, reveal.state);
            outColor = mix(outColor, capColor, capBlend);
        }
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

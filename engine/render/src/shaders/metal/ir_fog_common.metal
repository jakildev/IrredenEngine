#ifndef IR_FOG_COMMON_METAL_INCLUDED
#define IR_FOG_COMMON_METAL_INCLUDED

// Metal twin of ../ir_fog_common.glsl — the reveal model, constants, and the
// reveal / apply split are documented there. The reveal loop and the apply
// body must normalize equal to the GLSL twin (FogCrossSectionShaderParity).

#include "ir_iso_common.metal"
#include "ir_fog_los.metal"

constant int kFogOfWarHalfExtent = 128;
constant float kFogExploredValue = 128.0f / 255.0f;
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
    float4 visionCircleHeights[kMaxFogVisionCircles];
    float4 unexploredColor;
    // Per-source line-of-sight softness, source i at [i / 4][i % 4]: < 0 is the
    // hard gate, >= 0 the smooth gate with that band in voxels.
    float4 losSoftness[2];
};

struct FogReveal {
    float state;
    float gridState;
    float hardDistPastRim;
};

inline float fogTap(
    int2 cell,
    int2 fogSize,
    texture2d<float, access::read> canvasFogOfWar
) {
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0f;
    }
    return canvasFogOfWar.read(uint2(cell)).r;
}

inline float3 fogStateColor(float state, float3 sourceColor, float3 unexplored) {
    const float luminance = dot(sourceColor, float3(0.299f, 0.587f, 0.114f));
    const float3 exploredColor = float3(luminance) * 0.4f;
    if (state >= kFogExploredValue) {
        const float t = (state - kFogExploredValue) / (1.0f - kFogExploredValue);
        return mix(exploredColor, sourceColor, t);
    }
    const float t = state / kFogExploredValue;
    return mix(unexplored, exploredColor, t);
}

inline bool fogLosSmoothSampleNeeded(
    bool fogWholeBody,
    constant FogObserverData& fogObservers
) {
    if (fogWholeBody) {
        return false;
    }
    for (int i = 0; i < fogObservers.visionCircleCount; ++i) {
        if (fogLosSourceGated(fogObservers.losSourceMask, i) &&
            fogObservers.losSoftness[i >> 2][i & 3] >= 0.0f) {
            return true;
        }
    }
    return false;
}

inline FogLosSample fogLosVoxelSample(float3 pos3D, int faceId) {
    if ((faceId >> 1) == kZFace) {
        return fogLosSurfaceSample(pos3D);
    }
    return fogLosFaceSample(roundHalfUp(pos3D), faceId);
}

inline FogReveal fogRevealSample(
    float3 pos3D,
    float aaFloor,
    bool fogWholeBody,
    FogLosSample losSample,
    constant FogObserverData& fogObservers,
    texture2d<float, access::read> canvasFogOfWar,
    texture2d<float, access::read> fogLineOfSight
) {
    const int3 surfaceVoxel = roundHalfUp(pos3D);
    const int2 fogCell = surfaceVoxel.xy + int2(kFogOfWarHalfExtent);
    const int2 fogSize = int2(
        int(canvasFogOfWar.get_width()),
        int(canvasFogOfWar.get_height())
    );
    const float gridState = fogTap(fogCell, fogSize, canvasFogOfWar);
    float state = gridState;
    float hardDistPastRim = kFogRimFadeCells;
    FogLosTaps losTaps0;
    FogLosTaps losTaps1;
    bool losTapsLoaded0 = false;
    bool losTapsLoaded1 = false;

    for (int i = 0; i < fogObservers.visionCircleCount; ++i) {
        float losVisibility = 1.0f;
        const float softness = fogObservers.losSoftness[i >> 2][i & 3];
        if (fogLosSourceGated(fogObservers.losSourceMask, i) && !fogWholeBody && softness < 0.0f &&
            !fogLosVisible(surfaceVoxel, i, fogLineOfSight)) {
            continue;
        }
        if (fogLosSourceGated(fogObservers.losSourceMask, i) && !fogWholeBody && softness >= 0.0f) {
            if (i < kFogLosSourcesPerTile) {
                if (!losTapsLoaded0) {
                    losTaps0 = fogLosLoadTaps(losSample, 0, fogLineOfSight);
                    losTapsLoaded0 = true;
                }
                losVisibility = fogLosSmoothVisibility(losTaps0, i, losSample, softness);
            } else {
                if (!losTapsLoaded1) {
                    losTaps1 = fogLosLoadTaps(losSample, 1, fogLineOfSight);
                    losTapsLoaded1 = true;
                }
                losVisibility = fogLosSmoothVisibility(losTaps1, i, losSample, softness);
            }
            if (losVisibility <= 0.0f) {
                continue;
            }
        }
        const float4 heights = fogObservers.visionCircleHeights[i];
        const float zCostUp = fogWholeBody ? 0.0f : heights.y;
        const float zCostDown = fogWholeBody ? 0.0f : heights.z;
        const float dzUp = max(heights.x - pos3D.z, 0.0f);
        const float dzDown = max(pos3D.z - heights.x, 0.0f);
        const float distEff = length(pos3D.xy - fogObservers.visionCircles[i].xy) +
            zCostUp * max(dzUp - heights.w, 0.0f) +
            zCostDown * max(dzDown - heights.w, 0.0f);
        const float aa = max(fogObservers.visionCircles[i].w, aaFloor);
        const float reveal = losVisibility * (1.0f - smoothstep(
            fogObservers.visionCircles[i].z - aa,
            fogObservers.visionCircles[i].z + aa,
            distEff
        ));
        state = max(state, reveal);
        if (fogObservers.visionCircles[i].w == 0.0f) {
            const float distPastRim = distEff - fogObservers.visionCircles[i].z;
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

#endif // IR_FOG_COMMON_METAL_INCLUDED

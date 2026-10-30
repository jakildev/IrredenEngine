#ifndef IR_FOG_APPLY_METAL_INCLUDED
#define IR_FOG_APPLY_METAL_INCLUDED

// Display-space fog composition has no texture or observer-buffer dependency.
#include "ir_fog_color.metal"

constant float kFogCutTone = 0.85f;
constant float kFogCutMaxRimCells = 2.0f;
// MUST equal kFogHiddenKeepCells (ir_voxel_face_select.metal).
constant float kFogRimFadeCells = 8.0f;
constant float kFogRimFadeLevel = 0.75f;

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

// GLSL twin: fogApplyCutCap in ../ir_fog_apply.glsl.
inline float4 fogApplyCutCap(float4 sourceColor, float capTone) {
    return float4(fogCutCapBlend(sourceColor.rgb, sourceColor.rgb, capTone, 0.0f, 1.0f), sourceColor.a);
}

inline float4 fogApplyReveal(
    FogReveal reveal,
    int faceAxis,
    float4 sourceColor,
    float4 unexploredColor,
    int visionCircleCount,
    float4 revealSurfaceTreatment
) {
    float3 outColor = fogStateColor(
        reveal.state,
        sourceColor.rgb,
        unexploredColor.rgb
    );
    if (reveal.gridState < kFogExploredValue) {
        const float u = 1.0f - smoothstep(0.0f, kFogRimFadeCells, reveal.hardDistPastRim);
        outColor = mix(outColor, sourceColor.rgb, kFogRimFadeLevel * u * u);
        if (visionCircleCount > 0 && faceAxis != 2) {
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
            revealSurfaceTreatment.z,
            reveal.state,
            reveal.styledBand
        );
    }
    return float4(outColor, sourceColor.a);
}

inline float4 fogApplyBody(
    float state,
    float4 sourceColor,
    float4 unexploredColor
) {
    return float4(
        fogStateColor(state, sourceColor.rgb, unexploredColor.rgb),
        sourceColor.a
    );
}

#endif

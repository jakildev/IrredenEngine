#ifndef IR_FOG_SHADOW_CASTER_METAL_INCLUDED
#define IR_FOG_SHADOW_CASTER_METAL_INCLUDED

constant int kFogShadowMaxSources = 8;

struct FogShadowObserverData {
    float4 visionCircles[kFogShadowMaxSources];
    int visionCircleCount;
    int losSourceMask;
    int windowOriginX;
    int windowOriginY;
    float4 visionCircleHeights[kFogShadowMaxSources];
    float4 unexploredColor;
    float4 losParams[kFogShadowMaxSources];
    uint4 visionCircleChannels[2];
    float4 visionCircleCeilings[kFogShadowMaxSources];
    float4 revealSurfaceTreatment;
};

inline bool fogFieldCastsSunShadow(
    float3 worldPos,
    bool fogBody,
    int ceilingEnabled,
    constant FogShadowObserverData& observers
) {
    if (fogBody || ceilingEnabled == 0 || observers.visionCircleCount == 0) {
        return true;
    }

    bool covered = false;
    for (int i = 0; i < observers.visionCircleCount; ++i) {
        if (length(worldPos.xy - observers.visionCircles[i].xy) >
            observers.visionCircles[i].z) {
            continue;
        }
        covered = true;
        const float dzUp = max(observers.visionCircleHeights[i].x - worldPos.z, 0.0f);
        if (fogCeilingVisibility(observers.visionCircleCeilings[i], dzUp) >= 0.5f) {
            return true;
        }
    }
    return !covered;
}

#endif

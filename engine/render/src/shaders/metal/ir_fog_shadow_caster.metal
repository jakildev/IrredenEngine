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

inline uint2 fogShadowCell(float3 worldPos, constant FogShadowObserverData& observers, texture2d<uint, access::read> fogShadowWindow) {
    const int2 size = int2(int(fogShadowWindow.get_width()), int(fogShadowWindow.get_height()));
    const int2 column = roundHalfUp(worldPos).xy;
    const int2 texel = fogWindowTexel(column,
        int2(observers.windowOriginX, observers.windowOriginY), size);
    return texel.x < 0 ? uint2(0u, kFogChannelDefault) : fogShadowWindow.read(uint2(texel)).rg;
}

inline bool fogFieldCastsSunShadow(
    float3 worldPos, bool fogBody, int ceilingEnabled, uint2 cell, constant FogShadowObserverData& observers
) {
    if (fogBody || ceilingEnabled == 0 || observers.visionCircleCount == 0) {
        return true;
    }
    return fogCeilingRetained(roundHalfUp(worldPos), cell,
        observers.visionCircles, observers.visionCircleHeights,
        observers.visionCircleCeilings, observers.visionCircleChannels,
        observers.visionCircleCount, observers.revealSurfaceTreatment, 0.0);
}

// A retained voxel gains a boundary where its formerly occupied neighbor was
// cut by the same FIELD policy. Existing exposed faces and BODY are unchanged.
inline bool fogVoxelFaceCastsSunShadow(
    bool exposed, float3 worldCenter, float3 outwardNormal, bool fogBody,
    int ceilingEnabled, uint2 neighborCell, constant FogShadowObserverData& observers
) {
    if (exposed) return true;
    if (fogBody || ceilingEnabled == 0) return false;
    return !fogFieldCastsSunShadow(
        worldCenter + outwardNormal, false, ceilingEnabled, neighborCell, observers);
}

#endif

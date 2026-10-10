// FIELD caster policy for the sun-shadow bake. The block mirrors
// FrameDataFogObservers; only the circle, height and ceiling lanes are read.

const int kFogShadowMaxSources = 8;

layout(std140, binding = 27) uniform FogShadowObserverData {
    vec4 fogShadowVisionCircles[kFogShadowMaxSources];
    int fogShadowVisionCircleCount;
    int fogShadowLosSourceMask;
    int fogShadowWindowOriginX;
    int fogShadowWindowOriginY;
    vec4 fogShadowVisionCircleHeights[kFogShadowMaxSources];
    vec4 fogShadowUnexploredColor;
    vec4 fogShadowLosParams[kFogShadowMaxSources];
    uvec4 fogShadowVisionCircleChannels[2];
    vec4 fogShadowVisionCircleCeilings[kFogShadowMaxSources];
    vec4 fogShadowRevealSurfaceTreatment;
};

bool fogFieldCastsSunShadow(vec3 worldPos, bool fogBody, int ceilingEnabled) {
    if (fogBody || ceilingEnabled == 0 || fogShadowVisionCircleCount == 0) {
        return true;
    }

    bool covered = false;
    for (int i = 0; i < fogShadowVisionCircleCount; ++i) {
        if (length(worldPos.xy - fogShadowVisionCircles[i].xy) >
            fogShadowVisionCircles[i].z) {
            continue;
        }
        covered = true;
        const float dzUp = max(fogShadowVisionCircleHeights[i].x - worldPos.z, 0.0);
        if (fogCeilingVisibility(fogShadowVisionCircleCeilings[i], dzUp) >= 0.5) {
            return true;
        }
    }
    return !covered;
}

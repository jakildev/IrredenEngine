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

// The caller declares the format-qualified image before this include. GLSL
// image parameters cannot retain a portable format qualifier across drivers.
uvec2 fogShadowCell(vec3 worldPos) {
    const ivec2 size = imageSize(fogShadowWindow);
    const ivec2 column = roundHalfUp(worldPos).xy;
    const ivec2 texel = fogWindowTexel(column,
        ivec2(fogShadowWindowOriginX, fogShadowWindowOriginY), size);
    return texel.x < 0 ? uvec2(0u, kFogChannelDefault) : imageLoad(fogShadowWindow, texel).rg;
}

bool fogFieldCastsSunShadow(
    vec3 worldPos, bool fogBody, int ceilingEnabled, uvec2 cell
) {
    if (fogBody || ceilingEnabled == 0 || fogShadowVisionCircleCount == 0) {
        return true;
    }
    return fogCeilingRetained(roundHalfUp(worldPos), cell,
        fogShadowVisionCircles, fogShadowVisionCircleHeights,
        fogShadowVisionCircleCeilings, fogShadowVisionCircleChannels,
        fogShadowVisionCircleCount, fogShadowRevealSurfaceTreatment, 0.0);
}

// A retained voxel gains a boundary where its formerly occupied neighbor was
// cut by the same FIELD policy. Existing exposed faces and BODY are unchanged.
bool fogVoxelFaceCastsSunShadow(
    bool exposed, vec3 worldCenter, vec3 outwardNormal, bool fogBody,
    int ceilingEnabled, uvec2 neighborCell
) {
    if (exposed) return true;
    if (fogBody || ceilingEnabled == 0) return false;
    return !fogFieldCastsSunShadow(
        worldCenter + outwardNormal, false, ceilingEnabled, neighborCell);
}

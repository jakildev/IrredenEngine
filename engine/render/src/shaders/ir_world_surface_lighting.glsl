#ifndef IR_WORLD_SURFACE_LIGHTING_GLSL_INCLUDED
#define IR_WORLD_SURFACE_LIGHTING_GLSL_INCLUDED

#include "ir_world_lighting.glsl"
#include "ir_lighting_frame_data.glsl"
#include "ir_surface_light_volume.glsl"
#include "ir_surface_material.glsl"

// Requires ir_sun_shadow_sample.glsl after its specialization defines.
layout(binding = 3) uniform sampler2D paletteLUT;
layout(binding = 5) uniform sampler3D lightVolume;
layout(std140, binding = 7) uniform SurfaceLightVolumeParams {
    int gridSize;
    int halfExtent;
    int lightCount;
    float stepFalloff;
    ivec4 lightVolumeWorldOrigin;
};

bool worldSurfaceNeedsSunShadow(float lambert) {
    return shadowsEnabled != 0 && lambert != 0.0 && sunIntensity != 0.0 && sunAmbient != 1.0;
}

vec3 composeWorldSurfaceLighting(vec3 albedo, float ao, vec3 normal, float lambert,
                                 float visibility, vec3 localLightPosition) {
    const vec3 material = surfaceMaterialColor(albedo, ao, lutEnabled != 0, paletteLUT);
    vec3 linearColor = material * surfaceSunFactor(sunAmbient, sunIntensity, lambert, visibility);
    if (lightVolumeEnabled != 0)
        linearColor += albedo * surfaceLightVolume(localLightPosition, lightVolumeWorldOrigin, lightVolume);
    if (hdrEnabled != 0 && skyIntensity > 0.0)
        linearColor += surfaceSkyLight(normal, skyColor.rgb, skyIntensity, ao);
    return surfaceDisplayColor(linearColor, exposure, hdrEnabled != 0);
}

// Shadow geometry may vary across a face whose local-light sample stays fixed.
vec3 worldSurfaceLighting(vec3 albedo, float ao, vec3 position, vec3 normal,
                          vec4 casterRotation, vec3 localLightPosition) {
    const float lambert = max(0.0, dot(normal, sunDirection.xyz));
    const float visibility = worldSurfaceNeedsSunShadow(lambert) ?
        worldSurfaceSunShadowFactor(position, normal, pos3DtoDistance(position), casterRotation) :
        1.0;
    return composeWorldSurfaceLighting(albedo, ao, normal, lambert, visibility, localLightPosition);
}

#endif

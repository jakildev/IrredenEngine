#ifndef IR_WORLD_SURFACE_LIGHTING_METAL_INCLUDED
#define IR_WORLD_SURFACE_LIGHTING_METAL_INCLUDED

#include "ir_world_lighting.metal"
#include "ir_surface_light_volume.metal"
#include "ir_surface_material.metal"

// Requires ir_sun_shadow_sample.metal after its specialization defines.
inline bool worldSurfaceNeedsSunShadow(float lambert, constant FrameDataSun &sun) {
    return sun.shadowsEnabled != 0 && lambert != 0.0 && sun.sunIntensity != 0.0 &&
           sun.sunAmbient != 1.0;
}

inline float3 composeWorldSurfaceLighting(float3 albedo, float ao, float3 normal, float lambert,
                                          float visibility, float3 localLightPosition,
                                          constant FrameDataLightingToTrixel &lighting,
                                          constant LightVolumeParams &volumeParams,
                                          constant FrameDataSun &sun,
                                          device const GPULightSource *lights,
                                          texture2d<float> paletteLUT,
                                          texture3d<float> lightVolume,
                                          texture3d<float, access::read> lightVolumeId) {
    const float3 material = surfaceMaterialColor(albedo, ao, lighting.lutEnabled != 0, paletteLUT);
    float3 linearColor = material * surfaceSunFactor(sun.sunAmbient, sun.sunIntensity, lambert, visibility);
    if (lighting.lightVolumeEnabled != 0) {
        constexpr sampler volumeSampler(filter::linear, address::clamp_to_edge);
        linearColor += albedo * surfaceLightVolume(localLightPosition, volumeParams.worldOriginVoxel,
                                                   lightVolume, lightVolumeId, volumeSampler, lights);
    }
    if (lighting.hdrEnabled != 0 && lighting.skyIntensity > 0.0)
        linearColor += surfaceSkyLight(normal, lighting.skyColor.rgb, lighting.skyIntensity, ao);
    return surfaceDisplayColor(linearColor, lighting.exposure, lighting.hdrEnabled != 0);
}

// Shadow geometry may vary across a face whose local-light sample stays fixed.
inline float3 worldSurfaceLighting(float3 albedo, float ao, float3 position, float3 normal,
                                   float4 casterRotation, float3 localLightPosition,
                                   constant FrameDataLightingToTrixel &lighting,
                                   constant LightVolumeParams &volumeParams,
                                   constant FrameDataSun &sun,
                                   device const uint *sunDepthBuf,
                                   device const GPULightSource *lights,
                                   texture2d<float> paletteLUT,
                                   texture3d<float> lightVolume,
                                   texture3d<float, access::read> lightVolumeId) {
    const float lambert = max(0.0, dot(normal, sun.sunDirection.xyz));
    const float visibility = worldSurfaceNeedsSunShadow(lambert, sun) ?
        worldSurfaceSunShadowFactor(position, normal, pos3DtoDistance(position), casterRotation,
                                    sun, sunDepthBuf) : 1.0;
    return composeWorldSurfaceLighting(albedo, ao, normal, lambert, visibility,
                                       localLightPosition, lighting, volumeParams, sun, lights,
                                       paletteLUT, lightVolume, lightVolumeId);
}

#endif

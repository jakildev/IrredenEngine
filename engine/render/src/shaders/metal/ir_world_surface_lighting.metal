#ifndef IR_WORLD_SURFACE_LIGHTING_METAL_INCLUDED
#define IR_WORLD_SURFACE_LIGHTING_METAL_INCLUDED

#include "ir_world_lighting.metal"
#include "ir_surface_light_volume.metal"
#include "ir_surface_material.metal"

// Requires ir_sun_shadow_sample.metal after its specialization defines.
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
    const float3 material = surfaceMaterialColor(albedo, ao, lighting.lutEnabled != 0, paletteLUT);
    const float visibility = sun.shadowsEnabled == 0 ? 1.0 :
        worldSurfaceSunShadowFactor(position, normal, pos3DtoDistance(position), casterRotation,
                                    sun, sunDepthBuf);
    const float lambert = max(0.0, dot(normal, sun.sunDirection.xyz));
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

#endif

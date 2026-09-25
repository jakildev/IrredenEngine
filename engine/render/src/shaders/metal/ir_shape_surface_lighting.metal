#include "ir_world_lighting.metal"
#include "ir_surface_light_volume.metal"

inline float3 shapeSurfaceLighting(uint2 ownerPixel, int ownerWidth, float3 position, float3 normal,
                                   float cascadeDepth, float4 casterRotation, float3 fallbackColor,
                                   device const ShapeDescriptor *receiverShapes,
                                   device const uint *receiverOwners,
                                   device const ShapeTileDescriptor *receiverTiles,
                                   constant FrameDataLightingToTrixel &lighting,
                                   constant LightVolumeParams &volumeParams,
                                   constant FrameDataSun &sun,
                                   device const uint *sunDepthBuf,
                                   device const GPULightSource *lights,
                                   texture2d<float> paletteLUT, texture2d<float> surfaceAO,
                                   texture3d<float> lightVolume,
                                   texture3d<float, access::read> lightVolumeId) {
    const uint key = receiverOwners[ownerPixel.y * uint(ownerWidth) + ownerPixel.x];
    const int shapeIndex = receiverTiles[key / kShapeSamplesPerTile].shapeIndex;
    if ((receiverShapes[shapeIndex].flags & kShapeProceduralColorFlags) != 0u)
        return fallbackColor;
    const float3 albedo = unpackColor(receiverShapes[shapeIndex].color).rgb;
    const float ao = surfaceAO.read(ownerPixel).r;
    float3 material = albedo * ao;
    if (lighting.lutEnabled != 0) {
        constexpr sampler paletteSampler(filter::nearest, address::clamp_to_edge);
        const float luminance = dot(albedo, float3(0.299, 0.587, 0.114));
        material = albedo * paletteLUT.sample(paletteSampler, float2(ao, luminance), level(0.0)).rgb;
    }
    const float visibility = sun.shadowsEnabled == 0 ? 1.0 :
        worldShapeSurfaceSunShadowFactor(position, normal, cascadeDepth, casterRotation,
                                    sun, sunDepthBuf);
    const float lambert = max(0.0, dot(normal, sun.sunDirection.xyz));
    float3 linearColor = material * surfaceSunFactor(sun.sunAmbient, sun.sunIntensity, lambert, visibility);
    if (lighting.lightVolumeEnabled != 0) {
        constexpr sampler volumeSampler(filter::linear, address::clamp_to_edge);
        linearColor += albedo * surfaceLightVolume(position, volumeParams.worldOriginVoxel,
                                                   lightVolume, lightVolumeId, volumeSampler, lights);
    }
    if (lighting.hdrEnabled != 0 && lighting.skyIntensity > 0.0)
        linearColor += surfaceSkyLight(normal, lighting.skyColor.rgb, lighting.skyIntensity, ao);
    return surfaceDisplayColor(linearColor, lighting.exposure, lighting.hdrEnabled != 0);
}

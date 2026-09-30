#include "ir_world_surface_lighting.metal"

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
    const float lambert = max(0.0, dot(normal, sun.sunDirection.xyz));
    const float visibility = worldSurfaceNeedsSunShadow(lambert, sun) ?
        worldShapeSurfaceSunShadowFactor(position, normal, cascadeDepth, casterRotation,
                                        sun, sunDepthBuf) : 1.0;
    return composeWorldSurfaceLighting(albedo, ao, normal, lambert, visibility, position,
                                       lighting, volumeParams, sun, lights, paletteLUT,
                                       lightVolume, lightVolumeId);
}

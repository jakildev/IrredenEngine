#include "ir_world_lighting.glsl"
#include "ir_lighting_frame_data.glsl"
#include "ir_surface_light_volume.glsl"

layout(binding = 3) uniform sampler2D paletteLUT;
layout(binding = 4) uniform sampler2D surfaceAO;
layout(binding = 5) uniform sampler3D lightVolume;
layout(rgba8, binding = 7) readonly uniform image3D lightVolumeId;
layout(std140, binding = 7) uniform SurfaceLightVolumeParams {
    int gridSize;
    int halfExtent;
    int lightCount;
    float stepFalloff;
    ivec4 lightVolumeWorldOrigin;
};

vec3 shapeSurfaceLighting(ivec2 ownerPixel, int ownerWidth, vec3 position, vec3 normal,
                          vec4 casterRotation, vec3 fallbackColor) {
    const uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    const int shapeIndex = receiverTiles[key / kShapeSamplesPerTile].shapeIndex;
    if ((receiverShapes[shapeIndex].flags & kShapeProceduralColorFlags) != 0u)
        return fallbackColor;
    const vec3 albedo = unpackColor(receiverShapes[shapeIndex].color).rgb;
    const float ao = texelFetch(surfaceAO, ownerPixel, 0).r;
    vec3 material = albedo * ao;
    if (lutEnabled != 0) {
        const float luminance = dot(albedo, vec3(0.299, 0.587, 0.114));
        material = albedo * textureLod(paletteLUT, vec2(ao, luminance), 0.0).rgb;
    }
    const float visibility = shadowsEnabled == 0 ? 1.0 :
        worldSurfaceSunShadowFactor(position, normal, pos3DtoDistance(position), casterRotation);
    const float lambert = max(0.0, dot(normal, sunDirection.xyz));
    vec3 linearColor = material * surfaceSunFactor(sunAmbient, sunIntensity, lambert, visibility);
    if (lightVolumeEnabled != 0)
        linearColor += albedo * surfaceLightVolume(position, lightVolumeWorldOrigin,
                                                   lightVolume, lightVolumeId);
    if (hdrEnabled != 0 && skyIntensity > 0.0)
        linearColor += surfaceSkyLight(normal, skyColor.rgb, skyIntensity, ao);
    return surfaceDisplayColor(linearColor, exposure, hdrEnabled != 0);
}

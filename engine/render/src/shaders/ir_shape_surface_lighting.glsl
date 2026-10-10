#include "ir_world_surface_lighting.glsl"

layout(binding = 4) uniform sampler2D surfaceAO;

vec3 shapeSurfaceLighting(ivec2 ownerPixel, int ownerWidth, vec3 position, vec3 normal,
                          float cascadeDepth, vec4 casterRotation, vec3 fallbackColor) {
    const uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    const int shapeIndex = receiverTiles[key / kShapeSamplesPerTile].shapeIndex;
    if ((receiverShapes[shapeIndex].flags & kShapeProceduralColorFlags) != 0u)
        return fallbackColor;
    if (debugOverlayMode == 8)
        return normal * 0.5 + 0.5;
    const vec3 albedo = unpackColor(receiverShapes[shapeIndex].color).rgb;
    const float ao = texelFetch(surfaceAO, ownerPixel, 0).r;
    const float lambert = max(0.0, dot(normal, sunDirection.xyz));
    const float visibility = worldSurfaceNeedsSunShadow(lambert) ?
        worldShapeSurfaceSunShadowFactor(position, normal, cascadeDepth, casterRotation) : 1.0;
    return composeWorldSurfaceLighting(albedo, ao, normal, lambert, visibility, position);
}

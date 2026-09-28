#include "ir_world_surface_lighting.glsl"

layout(binding = 4) uniform sampler2D surfaceAO;

vec3 shapeSurfaceLighting(ivec2 ownerPixel, int ownerWidth, vec3 position, vec3 normal,
                          vec4 casterRotation, vec3 fallbackColor) {
    const uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    const int shapeIndex = receiverTiles[key / kShapeSamplesPerTile].shapeIndex;
    if ((receiverShapes[shapeIndex].flags & kShapeProceduralColorFlags) != 0u)
        return fallbackColor;
    const vec3 albedo = unpackColor(receiverShapes[shapeIndex].color).rgb;
    const float ao = texelFetch(surfaceAO, ownerPixel, 0).r;
    return worldSurfaceLighting(albedo, ao, position, normal, casterRotation, position);
}

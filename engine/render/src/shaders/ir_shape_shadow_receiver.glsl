// A retained analytical sample owns one camera ray, before its six display offsets.
// Shadow geometry may follow that ray without changing the displayed face normal.
bool shapeSphereShadowReceiver(ShapeDescriptor shape, ShapeProjectionData projection,
                               ivec2 sampleIso, out vec3 position) {
    if (shape.shapeType != SHAPE_SPHERE || (shape.flags & 1u) != 0u ||
        projection.finiteCoverage == 0 || projection.voxelRenderOptions.x == 0 ||
        projection.voxelRenderOptions.y <= 1 || projection.latticeShapes != 0 ||
        (projection.smoothYawEnabled == 0 && projection.residualYaw != 0.0)) return false;
    int density = projection.voxelRenderOptions.y;
    vec2 yaw = shapeYawCosSin(projection);
    vec3 center = shapeRenderedCenter(shape.worldPosition.xyz, projection);
    ivec2 originIso = shapeRenderedOriginIso(center, projection, density);
    vec3 origin = isoPositionToPos3D(vec2(sampleIso - originIso), 0.0) / float(density);
    vec3 direction = normalize(vec3(1.0));
    float radius = shape.params.x + 0.5 / float(density);
    if (radius <= 0.0) return false;
    float bound = length(origin) + radius;
    float entry;
    if (!sdfRaySphere(origin, direction, radius, -bound, bound, entry)) return false;
    vec3 viewOffset = origin + direction * entry;
    vec3 worldOffset = vec3(yaw.x * viewOffset.x - yaw.y * viewOffset.y,
                            yaw.y * viewOffset.x + yaw.x * viewOffset.y, viewOffset.z);
    position = center + worldOffset;
    return true;
}

bool selectedShapeSphereShadowReceiver(ivec2 ownerPixel, int ownerWidth,
                                       out vec3 position) {
    if (receiverFrame.shapeCount <= 0) return false;
    uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    if (key == 0xffffffffu) return false;
    ShapeTileDescriptor tile = receiverTiles[key / kShapeSamplesPerTile];
    uint lane = (key / 6u) % 64u;
    ivec2 sampleIso = tile.tileIsoOrigin + ivec2(int(lane % 8u), int(lane / 8u));
    return shapeSphereShadowReceiver(receiverShapes[tile.shapeIndex], receiverFrame,
                                      sampleIso, position);
}

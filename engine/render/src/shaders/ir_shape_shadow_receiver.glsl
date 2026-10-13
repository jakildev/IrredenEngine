// A retained analytical sample owns one camera ray, before its six display offsets.
// Shadow geometry may follow that ray without changing the displayed face normal.
bool shapeAnalyticShadowReceiver(ShapeDescriptor shape, ShapeProjectionData projection,
                               ivec2 sampleIso, out vec3 position) {
    bool supported = shape.shapeType == SHAPE_SPHERE || shape.shapeType == SHAPE_CYLINDER;
    if (!supported || (shape.flags & 1u) != 0u ||
        projection.finiteCoverage == 0 || projection.voxelRenderOptions.x == 0 ||
        projection.voxelRenderOptions.y <= 1 || projection.latticeShapes != 0 ||
        (projection.smoothYawEnabled == 0 && projection.residualYaw != 0.0)) return false;
    int density = projection.voxelRenderOptions.y;
    vec2 yaw = shapeYawCosSin(projection);
    vec3 center = shapeRenderedCenter(shape.worldPosition.xyz, projection);
    ivec2 originIso = shapeRenderedOriginIso(center, projection, density);
    vec3 origin = isoPositionToPos3D(vec2(sampleIso - originIso), 0.0) / float(density);
    vec3 direction = normalize(vec3(1.0));
    vec3 viewOffset;
    if (shape.shapeType == SHAPE_SPHERE) {
        float radius = shape.params.x + 0.5 / float(density);
        if (radius <= 0.0) return false;
        float bound = length(origin) + radius;
        float entry;
        if (!sdfRaySphere(origin, direction, radius, -bound, bound, entry)) return false;
        viewOffset = origin + direction * entry;
    } else {
        vec3 worldOrigin = vec3(yaw.x * origin.x - yaw.y * origin.y,
                                  yaw.y * origin.x + yaw.x * origin.y, origin.z);
        vec3 worldDirection = vec3(yaw.x * direction.x - yaw.y * direction.y,
                                     yaw.y * direction.x + yaw.x * direction.y, direction.z);
        vec3 localOrigin = worldOrigin;
        vec3 localDirection = worldDirection;
        if (shapeHasEntityRotation(shape.rotation)) {
            localOrigin = rotateByInverseQuat(worldOrigin, shape.rotation);
            localDirection = rotateByInverseQuat(worldDirection, shape.rotation);
        }
        float threshold = 0.5 / float(density);
        vec3 extent = shapeSunHalfExtent(shape.shapeType, shape.params, threshold);
        float nearDepth = -1e30;
        float farDepth = 1e30;
        for (int axis = 0; axis < 3; ++axis) {
            if (!sdfRaySlab(localOrigin[axis], localDirection[axis], -extent[axis],
                            extent[axis], nearDepth, farDepth)) return false;
        }
        float entry;
        if (!sdfRayHit(shape.shapeType, shape.params, localOrigin, localDirection,
                       threshold, nearDepth, farDepth, entry)) return false;
        viewOffset = origin + direction * entry;
    }
    vec3 worldOffset = vec3(yaw.x * viewOffset.x - yaw.y * viewOffset.y,
                            yaw.y * viewOffset.x + yaw.x * viewOffset.y, viewOffset.z);
    position = center + worldOffset;
    return true;
}

bool selectedShapeAnalyticShadowReceiver(ivec2 ownerPixel, int ownerWidth,
                                       out vec3 position) {
    if (receiverFrame.shapeCount <= 0) return false;
    uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    if (key == 0xffffffffu) return false;
    ShapeTileDescriptor tile = receiverTiles[key / kShapeSamplesPerTile];
    uint lane = (key / 6u) % 64u;
    ivec2 sampleIso = tile.tileIsoOrigin + ivec2(int(lane % 8u), int(lane / 8u));
    return shapeAnalyticShadowReceiver(receiverShapes[tile.shapeIndex], receiverFrame,
                                      sampleIso, position);
}

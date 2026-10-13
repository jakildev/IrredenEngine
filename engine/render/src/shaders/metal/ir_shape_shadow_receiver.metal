// A retained analytical sample owns one camera ray, before its six display offsets.
// Shadow geometry may follow that ray without changing the displayed face normal.
inline bool shapeAnalyticShadowReceiver(ShapeDescriptor shape, constant ShapeProjectionData &projection,
                               int2 sampleIso, thread float3 &position) {
    bool supported = shape.shapeType == SHAPE_SPHERE || shape.shapeType == SHAPE_CYLINDER;
    if (!supported || (shape.flags & 1u) != 0u ||
        projection.finiteCoverage == 0 || projection.voxelRenderOptions.x == 0 ||
        projection.voxelRenderOptions.y <= 1 || projection.latticeShapes != 0 ||
        (projection.smoothYawEnabled == 0 && projection.residualYaw != 0.0)) return false;
    int density = projection.voxelRenderOptions.y;
    float2 yaw = shapeYawCosSin(projection);
    float3 center = shapeRenderedCenter(shape.worldPosition.xyz, projection);
    int2 originIso = shapeRenderedOriginIso(center, projection, density);
    float3 origin = isoPositionToPos3D(float2(sampleIso - originIso), 0.0) / float(density);
    float3 direction = normalize(float3(1.0));
    float3 viewOffset;
    if (shape.shapeType == SHAPE_SPHERE) {
        float radius = shape.params.x + 0.5 / float(density);
        if (radius <= 0.0) return false;
        float bound = length(origin) + radius;
        float entry;
        if (!sdfRaySphere(origin, direction, radius, -bound, bound, entry)) return false;
        viewOffset = origin + direction * entry;
    } else {
        float3 worldOrigin = float3(yaw.x * origin.x - yaw.y * origin.y,
                                  yaw.y * origin.x + yaw.x * origin.y, origin.z);
        float3 worldDirection = float3(yaw.x * direction.x - yaw.y * direction.y,
                                     yaw.y * direction.x + yaw.x * direction.y, direction.z);
        float3 localOrigin = worldOrigin;
        float3 localDirection = worldDirection;
        if (shapeHasEntityRotation(shape.rotation)) {
            localOrigin = rotateByInverseQuat(worldOrigin, shape.rotation);
            localDirection = rotateByInverseQuat(worldDirection, shape.rotation);
        }
        float threshold = 0.5 / float(density);
        float3 extent = shapeSunHalfExtent(shape.shapeType, shape.params, threshold);
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
    float3 worldOffset = float3(yaw.x * viewOffset.x - yaw.y * viewOffset.y,
                            yaw.y * viewOffset.x + yaw.x * viewOffset.y, viewOffset.z);
    position = center + worldOffset;
    return true;
}

inline bool selectedShapeAnalyticShadowReceiver(int2 ownerPixel, int ownerWidth,
                                       constant ShapeProjectionData &receiverFrame,
                                       device const ShapeDescriptor *receiverShapes,
                                       device const uint *receiverOwners,
                                       device const ShapeTileDescriptor *receiverTiles,
                                       thread float3 &position) {
    if (receiverFrame.shapeCount <= 0) return false;
    uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    if (key == 0xffffffffu) return false;
    ShapeTileDescriptor tile = receiverTiles[key / kShapeSamplesPerTile];
    uint lane = (key / 6u) % 64u;
    int2 sampleIso = tile.tileIsoOrigin + int2(int(lane % 8u), int(lane / 8u));
    return shapeAnalyticShadowReceiver(receiverShapes[tile.shapeIndex], receiverFrame,
                                      sampleIso, position);
}

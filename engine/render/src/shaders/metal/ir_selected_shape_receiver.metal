inline int selectedShapeIndex(int2 ownerPixel, int ownerWidth,
                             constant ShapeProjectionData &receiverFrame,
                             device const uint *receiverOwners,
                             device const ShapeTileDescriptor *receiverTiles) {
    if (receiverFrame.shapeCount <= 0) return -1;
    uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    if (key == 0xffffffffu) return -1;
    return receiverTiles[key / kShapeSamplesPerTile].shapeIndex;
}

// The stored owner selects geometry; the query may lie anywhere on that finite surface.
// Owner coordinates are in bounds for the retained submission. A miss preserves outputs.
inline bool selectedShapeBoxReceiver(int2 ownerPixel, int ownerWidth, float2 queryPixel,
                                     constant ShapeProjectionData &receiverFrame,
                                     device const ShapeDescriptor *receiverShapes,
                                     device const uint *receiverOwners,
                                     device const ShapeTileDescriptor *receiverTiles,
                                     bool recoverMiss,
                                     thread float3 &position, thread float3 &normal) {
    int shapeIndex = selectedShapeIndex(ownerPixel, ownerWidth,
                                       receiverFrame, receiverOwners, receiverTiles);
    if (shapeIndex < 0) return false;
    float3 exactPosition, exactNormal;
    if (!shapeBoxReceiver(receiverShapes[shapeIndex], receiverFrame,
                          queryPixel, recoverMiss, exactPosition, exactNormal)) return false;
    position = exactPosition;
    normal = exactNormal;
    return true;
}

// Solid smooth-yaw boxes store their entered world face in the cardinal triplet.
// A missing emitter slab writes no owner; its legacy per-diamond slot cannot reach here.
inline bool selectedShapeBoxUsesWorldFaceSlot(int2 ownerPixel, int ownerWidth,
                                             constant ShapeProjectionData &receiverFrame,
                                             device const ShapeDescriptor *receiverShapes,
                                             device const uint *receiverOwners,
                                             device const ShapeTileDescriptor *receiverTiles) {
    int shapeIndex = selectedShapeIndex(ownerPixel, ownerWidth,
                                       receiverFrame, receiverOwners, receiverTiles);
    if (shapeIndex < 0) return false;
    ShapeDescriptor shape = receiverShapes[shapeIndex];
    bool smoothMode = receiverFrame.voxelRenderOptions.x != 0 &&
                      receiverFrame.voxelRenderOptions.y > 1;
    return shape.shapeType == SHAPE_BOX && (shape.flags & 1u) == 0u &&
           abs(shape.rotation.w) >= 0.9999 && receiverFrame.smoothYawEnabled != 0 &&
           (smoothMode || receiverFrame.latticeShapes == 0);
}

layout(std140, binding = 23) uniform ShapeReceiverFrame {
    ShapeProjectionData receiverFrame;
};
layout(std430, binding = 20) readonly buffer ReceiverShapes { ShapeDescriptor receiverShapes[]; };
layout(std430, binding = 22) readonly buffer ReceiverOwners { uint receiverOwners[]; };
layout(std430, binding = 30) readonly buffer ReceiverTiles { ShapeTileDescriptor receiverTiles[]; };

int selectedShapeIndex(ivec2 ownerPixel, int ownerWidth) {
    if (receiverFrame.shapeCount <= 0) return -1;
    uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    if (key == 0xffffffffu) return -1;
    return receiverTiles[key / kShapeSamplesPerTile].shapeIndex;
}

// The stored owner selects geometry; the query may lie anywhere on that finite surface.
// Owner coordinates are in bounds for the retained submission. A miss preserves outputs.
bool selectedShapeBoxReceiver(ivec2 ownerPixel, int ownerWidth, vec2 queryPixel,
                              bool recoverMiss, inout vec3 position, inout vec3 normal) {
    int shapeIndex = selectedShapeIndex(ownerPixel, ownerWidth);
    if (shapeIndex < 0) return false;
    vec3 exactPosition, exactNormal;
    if (!shapeBoxReceiver(receiverShapes[shapeIndex], receiverFrame,
                          queryPixel, recoverMiss, exactPosition, exactNormal)) return false;
    position = exactPosition;
    normal = exactNormal;
    return true;
}

// Solid smooth-yaw boxes store their entered world face in the cardinal triplet.
// A missing emitter slab writes no owner; its legacy per-diamond slot cannot reach here.
bool selectedShapeBoxUsesWorldFaceSlot(ivec2 ownerPixel, int ownerWidth) {
    int shapeIndex = selectedShapeIndex(ownerPixel, ownerWidth);
    if (shapeIndex < 0) return false;
    ShapeDescriptor shape = receiverShapes[shapeIndex];
    bool smoothMode = receiverFrame.voxelRenderOptions.x != 0 &&
                      receiverFrame.voxelRenderOptions.y > 1;
    return shape.shapeType == SHAPE_BOX && (shape.flags & 1u) == 0u &&
           abs(shape.rotation.w) >= 0.9999 && receiverFrame.smoothYawEnabled != 0 &&
           (smoothMode || receiverFrame.latticeShapes == 0);
}

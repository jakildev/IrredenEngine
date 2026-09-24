layout(std140, binding = 23) uniform ShapeReceiverFrame {
    ShapeProjectionData receiverFrame;
};
layout(std430, binding = 20) readonly buffer ReceiverShapes { ShapeDescriptor receiverShapes[]; };
layout(std430, binding = 22) readonly buffer ReceiverOwners { uint receiverOwners[]; };
layout(std430, binding = 30) readonly buffer ReceiverTiles { ShapeTileDescriptor receiverTiles[]; };

// The stored owner selects geometry; the query may lie anywhere on that finite surface.
// Owner coordinates are in bounds for the retained submission. A miss preserves outputs.
bool selectedShapeBoxReceiver(ivec2 ownerPixel, int ownerWidth, vec2 queryPixel,
                              inout vec3 position, inout vec3 normal) {
    if (receiverFrame.shapeCount <= 0) return false;
    uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    if (key == 0xffffffffu) return false;
    int shapeIndex = receiverTiles[key / kShapeSamplesPerTile].shapeIndex;
    vec3 exactPosition, exactNormal;
    if (!shapeBoxReceiver(receiverShapes[shapeIndex], receiverFrame,
                          queryPixel, exactPosition, exactNormal)) return false;
    position = exactPosition;
    normal = exactNormal;
    return true;
}

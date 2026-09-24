// The stored owner selects geometry; the query may lie anywhere on that finite surface.
// Owner coordinates are in bounds for the retained submission. A miss preserves outputs.
inline bool selectedShapeBoxReceiver(int2 ownerPixel, int ownerWidth, float2 queryPixel,
                                     constant ShapeProjectionData &receiverFrame,
                                     device const ShapeDescriptor *receiverShapes,
                                     device const uint *receiverOwners,
                                     device const ShapeTileDescriptor *receiverTiles,
                                     thread float3 &position, thread float3 &normal) {
    if (receiverFrame.shapeCount <= 0) return false;
    uint key = receiverOwners[uint(ownerPixel.y * ownerWidth + ownerPixel.x)];
    if (key == 0xffffffffu) return false;
    int shapeIndex = receiverTiles[key / kShapeSamplesPerTile].shapeIndex;
    float3 exactPosition, exactNormal;
    if (!shapeBoxReceiver(receiverShapes[shapeIndex], receiverFrame,
                          queryPixel, exactPosition, exactNormal)) return false;
    position = exactPosition;
    normal = exactNormal;
    return true;
}

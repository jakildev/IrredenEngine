// Finite analytical geometry only: lattice, hollow and entity-rotated shapes
// retain their sampled-cell receiver until their own finite query is available.
inline bool shapeBoxReceiver(ShapeDescriptor shape, ShapeProjectionData projection,
                      float2 canvasPixel, thread float3& position, thread float3& normal) {
    bool smoothMode = projection.voxelRenderOptions.x != 0 &&
                      projection.voxelRenderOptions.y > 1;
    bool smoothYaw = projection.smoothYawEnabled != 0;
    if (shape.shapeType != SHAPE_BOX || (shape.flags & 1u) != 0u ||
        abs(shape.rotation.w) < 0.9999 ||
        (!smoothMode && (!smoothYaw || projection.latticeShapes != 0)) ||
        (!smoothYaw && projection.residualYaw != 0.0)) return false;

    int density = smoothMode ? projection.voxelRenderOptions.y : 1;
    float2 yaw = smoothYaw ? float2(cos(projection.visualYaw), sin(projection.visualYaw))
                        : cardinalYawCosSin(rasterYawCardinalIndex(projection.rasterYaw));
    float3 center = shape.worldPosition.xyz;
    float3 viewCenter = float3(yaw.x * center.x + yaw.y * center.y,
                         -yaw.y * center.x + yaw.x * center.y, center.z);
    int3 snappedCenter = roundHalfUp(viewCenter) * density;
    int2 originIso = smoothYaw
        ? roundHalfUp(pos3DtoPos2DIsoYawed(center * float(density), projection.visualYaw))
        : pos3DtoPos2DIso(snappedCenter);
    int2 frameOffset = trixelFrameOffset(projection.trixelCanvasOffsetZ1,
        projection.frameCanvasOffset, projection.voxelRenderOptions);
    float2 relativeIso = canvasPixel - float2(frameOffset + originIso);
    float3 halfExtent = (shape.params.xyz - 1.0) * (0.5 * float(density)) + float3(0.5);
    float entry, exitDepth;
    if (!boxSurfaceIntervalYaw(relativeIso.x, relativeIso.y, halfExtent,
                              yaw.x, yaw.y, entry, exitDepth, normal)) return false;
    float3 viewOffset = isoPositionToPos3D(relativeIso, entry);
    if (!smoothYaw) {
        float3 snapped = float3(snappedCenter) / float(density);
        center = float3(yaw.x * snapped.x - yaw.y * snapped.y,
                      yaw.y * snapped.x + yaw.x * snapped.y, snapped.z);
    }
    position = center + float3(yaw.x * viewOffset.x - yaw.y * viewOffset.y,
                            yaw.y * viewOffset.x + yaw.x * viewOffset.y,
                            viewOffset.z) / float(density);
    return true;
}

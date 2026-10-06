vec2 shapeYawCosSin(ShapeProjectionData projection) {
    return projection.smoothYawEnabled != 0
        ? vec2(cos(projection.visualYaw), sin(projection.visualYaw))
        : cardinalYawCosSin(rasterYawCardinalIndex(projection.rasterYaw));
}

// The world center c_shapes_to_trixel_body draws a shape around: its view-frame
// roundHalfUp `origin`, except smooth yaw off the lattice walk, which places the
// unsnapped position. Casters and receivers of the drawn surface both use it.
vec3 shapeRenderedCenter(vec3 worldPosition, ShapeProjectionData projection) {
    bool smoothMode = projection.voxelRenderOptions.x != 0 &&
                      projection.voxelRenderOptions.y > 1;
    if (projection.smoothYawEnabled != 0 && (smoothMode || projection.latticeShapes == 0))
        return worldPosition;
    vec2 yaw = shapeYawCosSin(projection);
    vec3 view = vec3(roundHalfUp(vec3(yaw.x * worldPosition.x + yaw.y * worldPosition.y,
                                         -yaw.y * worldPosition.x + yaw.x * worldPosition.y,
                                         worldPosition.z)));
    return vec3(yaw.x * view.x - yaw.y * view.y, yaw.y * view.x + yaw.x * view.y, view.z);
}

// Canvas cell receivers select their sun cascade by the canvas depth: the
// view-frame x+y+z at the canvas's subdivided resolution. A finite fragment that
// stands in for a canvas cell must select the same cascade as its neighbours.
float shapeCanvasIsoDepth(vec3 position, ShapeProjectionData projection) {
    float yaw = projection.smoothYawEnabled != 0 ? projection.visualYaw : projection.rasterYaw;
    int scale = projection.voxelRenderOptions.x != 0 ? max(projection.voxelRenderOptions.y, 1) : 1;
    return yawedIsoDistance(position, yaw) * float(scale);
}

// Finite analytical geometry only: lattice, hollow and entity-rotated shapes
// retain their sampled-cell receiver until their own finite query is available.
bool shapeBoxReceiver(ShapeDescriptor shape, ShapeProjectionData projection,
                      vec2 canvasPixel, bool recoverMiss,
                      out vec3 position, out vec3 normal) {
    bool smoothMode = projection.voxelRenderOptions.x != 0 &&
                      projection.voxelRenderOptions.y > 1;
    bool smoothYaw = projection.smoothYawEnabled != 0;
    if (shape.shapeType != SHAPE_BOX || (shape.flags & 1u) != 0u ||
        abs(shape.rotation.w) < 0.9999 ||
        (!smoothMode && (!smoothYaw || projection.latticeShapes != 0)) ||
        (!smoothYaw && projection.residualYaw != 0.0)) return false;

    int density = smoothMode ? projection.voxelRenderOptions.y : 1;
    vec2 yaw = shapeYawCosSin(projection);
    vec3 center = shapeRenderedCenter(shape.worldPosition.xyz, projection);
    // Cardinal yaw: center is a lattice cell rotated by an exact cos/sin pair,
    // so rotating it back recovers the integer view cell exactly.
    ivec2 originIso = smoothYaw
        ? roundHalfUp(pos3DtoPos2DIsoYawed(center * float(density), projection.visualYaw))
        : pos3DtoPos2DIso(roundHalfUp(vec3(yaw.x * center.x + yaw.y * center.y,
                                             -yaw.y * center.x + yaw.x * center.y,
                                             center.z)) * density);
    ivec2 frameOffset = trixelFrameOffset(projection.trixelCanvasOffsetZ1,
        projection.frameCanvasOffset, projection.voxelRenderOptions);
    vec2 relativeIso = canvasPixel - vec2(frameOffset + originIso);
    vec3 halfExtent = (shape.params.xyz - 1.0) * (0.5 * float(density)) + vec3(0.5);
    float entry, exitDepth;
    if (!boxSurfaceIntervalYaw(relativeIso.x, relativeIso.y, halfExtent,
                              yaw.x, yaw.y, entry, exitDepth, normal)) {
        if (!recoverMiss) return false;

        // An inset anchor keeps recovery local on elongated boxes while still
        // guaranteeing that the anchor ray intersects the finite box.
        vec3 rayDirection = vec3(yaw.x - yaw.y, yaw.x + yaw.y, 1.0) / 3.0;
        vec3 rayOrigin = vec3(
            -(yaw.x + yaw.y) * 0.5 * relativeIso.x -
                (yaw.x - yaw.y) * relativeIso.y / 6.0,
            (yaw.x - yaw.y) * 0.5 * relativeIso.x -
                (yaw.x + yaw.y) * relativeIso.y / 6.0,
            relativeIso.y / 3.0);
        vec3 weightedDirection = rayDirection / (halfExtent * halfExtent);
        float nearestDepth = -dot(weightedDirection, rayOrigin) /
                             dot(weightedDirection, rayDirection);
        float inset = min(halfExtent.x, min(halfExtent.y, halfExtent.z));
        vec3 interiorHalfExtent = halfExtent - vec3(inset);
        vec3 localAnchor = clamp(rayDirection * nearestDepth + rayOrigin,
                                 -interiorHalfExtent, interiorHalfExtent);
        vec2 viewAnchor = vec2(yaw.x * localAnchor.x + yaw.y * localAnchor.y,
                               -yaw.y * localAnchor.x + yaw.x * localAnchor.y);
        vec2 anchorIso = vec2(-viewAnchor.x + viewAnchor.y,
                              -viewAnchor.x - viewAnchor.y + 2.0 * localAnchor.z);

        float inside = 0.0;
        float outside = 1.0;
        vec2 recoveredIso = anchorIso;
        if (!boxSurfaceIntervalYaw(anchorIso.x, anchorIso.y, halfExtent,
                                   yaw.x, yaw.y, entry, exitDepth, normal)) return false;
        for (int iteration = 0; iteration < 12; ++iteration) {
            float middle = 0.5 * (inside + outside);
            vec2 candidateIso = anchorIso + (relativeIso - anchorIso) * middle;
            float candidateEntry, candidateExit;
            vec3 candidateNormal;
            if (boxSurfaceIntervalYaw(candidateIso.x, candidateIso.y, halfExtent,
                                      yaw.x, yaw.y, candidateEntry, candidateExit,
                                      candidateNormal)) {
                inside = middle;
                recoveredIso = candidateIso;
                entry = candidateEntry;
                exitDepth = candidateExit;
                normal = candidateNormal;
            } else {
                outside = middle;
            }
        }
        relativeIso = recoveredIso;
    }
    vec3 viewOffset = isoPositionToPos3D(relativeIso, entry);
    position = center + vec3(yaw.x * viewOffset.x - yaw.y * viewOffset.y,
                            yaw.y * viewOffset.x + yaw.x * viewOffset.y,
                            viewOffset.z) / float(density);
    return true;
}

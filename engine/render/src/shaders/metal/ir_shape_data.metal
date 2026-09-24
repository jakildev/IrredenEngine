constant uint kShapeSamplesPerTile = 8u * 8u * 3u * 2u;

struct ShapeProjectionData {
    float2 frameCanvasOffset;
    int2 trixelCanvasOffsetZ1;
    int2 canvasSize;
    int shapeCount;
    int padding0;
    int2 voxelRenderOptions;
    int2 cullIsoMin;
    int2 cullIsoMax;
    // Continuous Z-yaw is split into a cardinal-snap component (rasterYaw,
    // exact multiple of pi/2) and a residual component (residualYaw, in
    // [-pi/4, pi/4]). The cardinal path rasterizes at rasterYaw so its output
    // lines up with the voxel pool's cardinal-snap raster, then applies
    // faceDeform[face] to its sub-pixel offset to recover continuous yaw
    // geometrically.
    float visualYaw;
    float rasterYaw;
    float residualYaw;
    int tileGridX;
    // Smooth camera Z-yaw. 1 = continuous-yaw SDF path (full visualYaw query +
    // continuous center reposition + yawedIsoDistance depth); 0 = cardinal
    // rasterYaw + faceDeform path. Set per canvas (main world canvas only).
    // smoothYawEnabled and latticeShapes fill the 8 bytes before faceDeform so
    // the layout matches the std140 block and the C++ struct.
    int smoothYawEnabled;
    // 1 = a density-1 shape under smooth yaw is a lattice occupant: the
    // lattice walk with the continuous-yaw SDF query, anchored on the snapped
    // view cell, emitting the hexagons of the cells it covers (entity
    // canvases, whose voxels are lattice cells at every yaw).
    int latticeShapes;
    // Per-face deformation matrix packed column-major: .xy = col0, .zw = col1
    // of IRMath::faceDeformationMatrix(face, residualYaw). Identity when
    // residualYaw==0. Mirrors the GLSL `vec4 faceDeform[3]`.
    float4 faceDeform[3];
};

struct ShapeDescriptor {
    float4 worldPosition;
    float4 params;
    float4 rotation;
    uint shapeType;
    uint color;
    uint entityId;
    uint jointIndex;
    uint flags;
    uint lodLevel;
    uint pad0;
    uint pad1;
};

struct ShapeTileDescriptor {
    int shapeIndex;
    int pad0;
    int2 tileIsoOrigin;
};

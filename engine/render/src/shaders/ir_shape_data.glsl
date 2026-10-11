const uint kShapeSamplesPerTile = 8u * 8u * 3u * 2u;

struct ShapeProjectionData {
    vec2 frameCanvasOffset;
    ivec2 trixelCanvasOffsetZ1;
    ivec2 canvasSize;
    int shapeCount;
    int finiteCoverage;
    ivec2 voxelRenderOptions;
    ivec2 cullIsoMin;
    ivec2 cullIsoMax;
    float visualYaw;
    float rasterYaw;
    float residualYaw;
    int tileGridX;
    int smoothYawEnabled;
    int latticeShapes;
    vec4 faceDeform[3];
};

struct ShapeDescriptor {
    vec4 worldPosition;
    vec4 params;
    vec4 rotation;
    uint shapeType;
    uint color;
    uint entityId;
    uint jointIndex;
    uint flags;
    uint lodLevel;
    uint _pad0;
    uint _pad1;
};

struct ShapeTileDescriptor {
    int shapeIndex;
    int _pad0;
    ivec2 tileIsoOrigin;
};

const uint FLAG_CHECKERBOARD = 32u;
const uint FLAG_DEPTH_COLOR = 64u;
const uint kShapeProceduralColorFlags = FLAG_CHECKERBOARD | FLAG_DEPTH_COLOR;

int cardinalShapeStoredDepth(int unoffsetDepth, bool carriesLatticeOffset,
                             bool casterPass, bool correctCardinalDepth,
                             int subdivisions) {
    if (casterPass && correctCardinalDepth) {
        return unoffsetDepth;
    }
    return carriesLatticeOffset
        ? unoffsetDepth + cardinalRasterLatticeDepthOffset(subdivisions)
        : unoffsetDepth;
}

int cardinalShapeReceiverDepth(int storedDepth, int subdivisions, bool shapeOwned,
                              bool carriesLatticeOffset, bool finiteCoverage) {
    return shapeOwned && carriesLatticeOffset && finiteCoverage
        ? storedDepth - cardinalRasterLatticeDepthOffset(subdivisions)
        : storedDepth;
}

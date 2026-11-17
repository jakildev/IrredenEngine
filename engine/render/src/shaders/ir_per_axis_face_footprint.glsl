#ifndef IR_PER_AXIS_FACE_FOOTPRINT_INCLUDED
#define IR_PER_AXIS_FACE_FOOTPRINT_INCLUDED

// The framebuffer quad v_peraxis_scatter draws for one per-axis face, computed
// outside the vertex stage. Requires ir_iso_common.glsl ahead of it.
//
// KEEP IN SYNC with v_peraxis_scatter.glsl: every step from the encoded
// distance to the corner position and the corner depth mirrors that shader,
// through the same ir_iso_common helpers. The two are separate compilations,
// so a consumer compares against the hardware raster only through
// kFootprintPixelSlack and its own depth slack, never for equality.

// The scatter placement the footprint is projected with.
struct PerAxisScatterView {
    mat4 mpMatrix;
    vec4 cameraViewRotation;
    ivec4 visibleFaceIds;
    ivec4 perAxisStoreFrame;
    ivec2 perAxisBase;
    ivec2 canvasSize;      // per-axis store extent in cells
    vec2 framebufferSize;  // raster target extent in pixels
    float visualYaw;
    float encScale;        // kDepthEncodeShift * max(effective subdivisions, 1)
    float depthBias;       // distanceOffset - kMinTriangleDistance
    float depthRange;      // kMaxTriangleDistance - kMinTriangleDistance
};

// The scatter's own uniform blocks, under instance names so their members
// cannot collide with another frame-data block in the including shader.
layout(std140, binding = 1) uniform GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
} scatterGlobals;

layout(std140, binding = 3) uniform FrameDataIsoTriangles {
    mat4 mpMatrix;
    vec2 zoomLevel;
    vec2 canvasOffset;
    vec2 textureOffset;
    vec2 mouseHoveredTriangleIndex;
    vec2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    ivec2 perAxisBase;
    float visualYaw;
    int scatterDebugMode;
    ivec4 visibleFaceIds;
    vec4 _detachedResidualPad;
    vec4 _detachedDepthAxisPad;
    vec4 scatterFbResolution;
    int depthColorMode;
    float depthColorExtent;
    float _depthColorPad0;
    float _depthColorPad1;
    int overflowMode;
    int _overflowPad0;
    int _overflowPad1;
    int _overflowPad2;
    ivec4 perAxisStoreFrame;
    vec4 canvasFromPixel;
    ivec2 mouseFramebufferPixel;
    int hoverResolveMode;
    int gatherFilterDiagnostic;
    vec4 cameraViewRotation;
} scatterFrame;

// @p canvasSize is the per-axis store extent, which the vertex stage reads
// from its bound distance texture.
PerAxisScatterView perAxisScatterView(ivec2 canvasSize) {
    PerAxisScatterView view;
    view.mpMatrix = scatterFrame.mpMatrix;
    view.cameraViewRotation = scatterFrame.cameraViewRotation;
    view.visibleFaceIds = scatterFrame.visibleFaceIds;
    view.perAxisStoreFrame = scatterFrame.perAxisStoreFrame;
    view.perAxisBase = scatterFrame.perAxisBase;
    view.canvasSize = canvasSize;
    view.framebufferSize = scatterFrame.scatterFbResolution.xy;
    view.visualYaw = scatterFrame.visualYaw;
    view.encScale =
        float(kDepthEncodeShift) * max(scatterFrame.effectiveSubdivisionsForHover.x, 1.0);
    view.depthBias = float(scatterFrame.distanceOffset - scatterGlobals.kMinTriangleDistance);
    view.depthRange =
        float(scatterGlobals.kMaxTriangleDistance - scatterGlobals.kMinTriangleDistance);
    return view;
}

// Corners are indexed by the quad's corner selector: 0 = (0,0), 1 = (1,0),
// 2 = (0,1), 3 = (1,1). `pixel` is in raster-target texel coordinates, where
// texel (i, j) has its sample at (i + 0.5, j + 0.5) on both backends. `depth`
// is the vertex stage's vDepth, before the overflow lane's two-band offset.
struct PerAxisFaceFootprint {
    vec2 pixel[4];
    float depth[4];
};

// Outward slack on a footprint's texel bounds, in pixels. Covers the rounding
// difference between this projection and the vertex stage's.
const float kFootprintPixelSlack = 0.125;

vec3 perAxisFootprintSpanCorner(int axis, vec3 origin, vec2 cornerSel) {
    if (axis == 0) return origin + vec3(0.0, cornerSel.x, cornerSel.y);
    if (axis == 1) return origin + vec3(cornerSel.x, 0.0, cornerSel.y);
    return origin + vec3(cornerSel.x, cornerSel.y, 0.0);
}

PerAxisFaceFootprint perAxisFaceFootprint(ivec2 cell, int rawDist, PerAxisScatterView view) {
    const int slot = decodeSlot(rawDist);
    const int vFrac4 = decodeVFrac4PerAxis(rawDist);
    const int uFrac4 = decodeUFrac4PerAxis(rawDist);
    const int wFrac4 = decodeWFrac4PerAxis(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int rawDepth = decodeDepthPerAxis(rawDist);
    const int faceId = view.visibleFaceIds[slot] ^ flip;
    const int axis = faceId >> 1;

    vec3 eu, ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    const ivec2 isoPix = cell - view.perAxisStoreFrame.xy;
    const vec3 baseOrigin = rotateCardinalZInv(
        isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth)), view.perAxisStoreFrame.z
    );
    const vec3 origin = baseOrigin
        + eu * (float(uFrac4) / 16.0 - 0.5)
        + ev * (float(vFrac4) / 16.0 - 0.5)
        + faceOutOfPlaneUnitAxis(axis) * (float(wFrac4) / 16.0 - 0.5);

    const bool spatial = spatialViewEnabled(view.cameraViewRotation);
    const ivec2 reprojBase = view.perAxisBase + ivec2(1);
    PerAxisFaceFootprint footprint;
    for (int corner = 0; corner < 4; ++corner) {
        const vec2 cornerSel = vec2(float(corner & 1), float(corner >> 1));
        const vec3 worldCorner = perAxisFootprintSpanCorner(axis, origin, cornerSel);
        const vec2 centeredCornerIso =
            (vec2(reprojBase) - 0.5 * vec2(view.canvasSize)) +
            (spatial
                ? spatialViewIso(worldCorner - kVoxelRasterCellAnchor, view.cameraViewRotation)
                : pos3DtoPos2DIsoYawedCellAnchor(worldCorner, view.visualYaw));
        vec2 quadPos;
        quadPos.x = centeredCornerIso.x / float(view.canvasSize.x);
        quadPos.y = -centeredCornerIso.y / float(view.canvasSize.y);
        const vec4 clip = view.mpMatrix * vec4(quadPos, 1.0, 1.0);
        footprint.pixel[corner] = (clip.xy / clip.w * 0.5 + 0.5) * view.framebufferSize;
        const float cornerKey = spatial
            ? spatialViewDepth(worldCorner - kVoxelRasterCellAnchor, view.cameraViewRotation) *
                  view.encScale
            : scatterCompositeDepthKey(worldCorner, view.visualYaw, view.encScale);
        footprint.depth[corner] = (cornerKey + view.depthBias) / view.depthRange;
    }
    return footprint;
}

// Inclusive texel rectangle holding every sample the quad can cover, before
// clamping to the raster target. Empty when hi < lo on either axis.
void perAxisFootprintTexelBounds(PerAxisFaceFootprint footprint, out ivec2 lo, out ivec2 hi) {
    const vec2 pixelMin = min(
        min(footprint.pixel[0], footprint.pixel[1]), min(footprint.pixel[2], footprint.pixel[3])
    );
    const vec2 pixelMax = max(
        max(footprint.pixel[0], footprint.pixel[1]), max(footprint.pixel[2], footprint.pixel[3])
    );
    lo = ivec2(ceil(pixelMin - 0.5 - kFootprintPixelSlack));
    hi = ivec2(floor(pixelMax - 0.5 + kFootprintPixelSlack));
}

float perAxisFootprintNearestDepth(PerAxisFaceFootprint footprint) {
    return min(
        min(footprint.depth[0], footprint.depth[1]), min(footprint.depth[2], footprint.depth[3])
    );
}

#endif

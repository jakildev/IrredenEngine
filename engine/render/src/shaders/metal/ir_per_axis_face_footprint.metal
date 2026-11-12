// Mirrors shaders/ir_per_axis_face_footprint.glsl; keep the two in lockstep.
#ifndef IR_PER_AXIS_FACE_FOOTPRINT_METAL_INCLUDED
#define IR_PER_AXIS_FACE_FOOTPRINT_METAL_INCLUDED

#include "ir_iso_common.metal"

// The framebuffer quad v_peraxis_scatter draws for one per-axis face, computed
// outside the vertex stage.
//
// KEEP IN SYNC with peraxis_scatter.metal: every step from the encoded
// distance to the corner position and the corner depth mirrors that shader,
// through the same ir_iso_common helpers. The two are separate compilations,
// so a consumer compares against the hardware raster only through
// kFootprintPixelSlack and its own depth slack, never for equality.

// The scatter placement the footprint is projected with.
struct PerAxisScatterView {
    float4x4 mpMatrix;
    float4 cameraViewRotation;
    int4 visibleFaceIds;
    int4 perAxisStoreFrame;
    int2 perAxisBase;
    int2 canvasSize;        // per-axis store extent in cells
    float2 framebufferSize; // raster target extent in pixels
    float visualYaw;
    float encScale;         // kDepthEncodeShift * max(effective subdivisions, 1)
    float depthBias;        // distanceOffset - kMinTriangleDistance
    float depthRange;       // kMaxTriangleDistance - kMinTriangleDistance
};

// The scatter's own uniform blocks: buffer 1 and buffer 3 of the draw.
struct PerAxisScatterGlobals {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

struct PerAxisScatterFrame {
    float4x4 mpMatrix;
    float2 zoomLevel;
    float2 canvasOffset;
    float2 textureOffset;
    float2 mouseHoveredTriangleIndex;
    float2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    int2 perAxisBase;
    float visualYaw;
    int scatterDebugMode;
    int4 visibleFaceIds;
    float4 _detachedResidualPad;
    float4 _detachedDepthAxisPad;
    float4 scatterFbResolution;
    int depthColorMode;
    float depthColorExtent;
    float _depthColorPad0;
    float _depthColorPad1;
    int overflowMode;
    int _overflowPad0;
    int _overflowPad1;
    int _overflowPad2;
    int4 perAxisStoreFrame;
    float4 canvasFromPixel;
    int2 mouseFramebufferPixel;
    int hoverResolveMode;
    int gatherFilterDiagnostic;
    float4 cameraViewRotation;
};

// @p canvasSize is the per-axis store extent, which the vertex stage reads
// from its bound distance texture.
inline PerAxisScatterView perAxisScatterView(
    constant PerAxisScatterFrame& frame, constant PerAxisScatterGlobals& globals, int2 canvasSize
) {
    PerAxisScatterView view;
    view.mpMatrix = frame.mpMatrix;
    view.cameraViewRotation = frame.cameraViewRotation;
    view.visibleFaceIds = frame.visibleFaceIds;
    view.perAxisStoreFrame = frame.perAxisStoreFrame;
    view.perAxisBase = frame.perAxisBase;
    view.canvasSize = canvasSize;
    view.framebufferSize = frame.scatterFbResolution.xy;
    view.visualYaw = frame.visualYaw;
    view.encScale = float(kDepthEncodeShift) * max(frame.effectiveSubdivisionsForHover.x, 1.0f);
    view.depthBias = float(frame.distanceOffset - globals.kMinTriangleDistance);
    view.depthRange = float(globals.kMaxTriangleDistance - globals.kMinTriangleDistance);
    return view;
}

// Corners are indexed by the quad's corner selector: 0 = (0,0), 1 = (1,0),
// 2 = (0,1), 3 = (1,1). `pixel` is in raster-target texel coordinates, where
// texel (i, j) has its sample at (i + 0.5, j + 0.5) on both backends: the
// vertex stage's clip-space Y negation and Metal's top-left texel origin
// cancel. `depth` is the vertex stage's depth, before the overflow lane's
// two-band offset.
struct PerAxisFaceFootprint {
    float2 pixel[4];
    float depth[4];
};

// Outward slack on a footprint's texel bounds, in pixels. Covers the rounding
// difference between this projection and the vertex stage's.
constant float kFootprintPixelSlack = 0.125f;

inline float3 perAxisFootprintSpanCorner(int axis, float3 origin, float2 cornerSel) {
    if (axis == 0) return origin + float3(0.0f, cornerSel.x, cornerSel.y);
    if (axis == 1) return origin + float3(cornerSel.x, 0.0f, cornerSel.y);
    return origin + float3(cornerSel.x, cornerSel.y, 0.0f);
}

inline PerAxisFaceFootprint perAxisFaceFootprint(
    int2 cell, int rawDist, PerAxisScatterView view
) {
    const int slot = decodeSlot(rawDist);
    const int vFrac4 = decodeVFrac4PerAxis(rawDist);
    const int uFrac4 = decodeUFrac4PerAxis(rawDist);
    const int wFrac4 = decodeWFrac4PerAxis(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int rawDepth = decodeDepthPerAxis(rawDist);
    const int faceId = view.visibleFaceIds[slot] ^ flip;
    const int axis = faceId >> 1;

    float3 eu, ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    const int2 isoPix = cell - view.perAxisStoreFrame.xy;
    const float3 baseOrigin = rotateCardinalZInv(
        isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth)), view.perAxisStoreFrame.z
    );
    const float3 origin = baseOrigin
        + eu * (float(uFrac4) / 16.0f - 0.5f)
        + ev * (float(vFrac4) / 16.0f - 0.5f)
        + faceOutOfPlaneUnitAxis(axis) * (float(wFrac4) / 16.0f - 0.5f);

    const bool spatial = spatialViewEnabled(view.cameraViewRotation);
    const int2 reprojBase = view.perAxisBase + int2(1);
    PerAxisFaceFootprint footprint;
    for (int corner = 0; corner < 4; ++corner) {
        const float2 cornerSel = float2(float(corner & 1), float(corner >> 1));
        const float3 worldCorner = perAxisFootprintSpanCorner(axis, origin, cornerSel);
        const float2 centeredCornerIso =
            (float2(reprojBase) - 0.5f * float2(view.canvasSize)) +
            (spatial
                ? spatialViewIso(worldCorner - kVoxelRasterCellAnchor, view.cameraViewRotation)
                : pos3DtoPos2DIsoYawedCellAnchor(worldCorner, view.visualYaw));
        float2 quadPos;
        quadPos.x = centeredCornerIso.x / float(view.canvasSize.x);
        quadPos.y = -centeredCornerIso.y / float(view.canvasSize.y);
        const float4 clip = view.mpMatrix * float4(quadPos, 1.0f, 1.0f);
        footprint.pixel[corner] = (clip.xy / clip.w * 0.5f + 0.5f) * view.framebufferSize;
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
inline void perAxisFootprintTexelBounds(
    PerAxisFaceFootprint footprint, thread int2& lo, thread int2& hi
) {
    const float2 pixelMin = min(
        min(footprint.pixel[0], footprint.pixel[1]), min(footprint.pixel[2], footprint.pixel[3])
    );
    const float2 pixelMax = max(
        max(footprint.pixel[0], footprint.pixel[1]), max(footprint.pixel[2], footprint.pixel[3])
    );
    lo = int2(ceil(pixelMin - 0.5f - kFootprintPixelSlack));
    hi = int2(floor(pixelMax - 0.5f + kFootprintPixelSlack));
}

inline float perAxisFootprintNearestDepth(PerAxisFaceFootprint footprint) {
    return min(
        min(footprint.depth[0], footprint.depth[1]), min(footprint.depth[2], footprint.depth[3])
    );
}

#endif

// Project: Irreden Engine
// File: metal/peraxis_scatter_detached.metal
// Detached-entity SO(3) — P3b (#1475) forward-scatter composite (Metal mirror
// of v_peraxis_scatter_detached.glsl). The per-DETACHED-entity analog of
// peraxis_scatter.metal: each instance is one cell of a rotating detached
// entity's per-axis canvas; the vertex stage recovers the model face origin and
// projects its deformed face quad under the octahedral-snap RESIDUAL quaternion,
// placed by the entity's mpMatrix. Reuses f_peraxis_scatter (peraxis_scatter.metal)
// for the fragment stage.

#include <metal_stdlib>
using namespace metal;

#include "ir_iso_common.metal"

struct VertexIn {
    float2 position [[attribute(0)]];  // unit quad corner in [-0.5, 0.5]^2
};

struct GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

// Shared with trixel_to_framebuffer.metal + peraxis_scatter.metal (buffer 3);
// the detached scatter reads the two P3b fields appended after visibleFaceIds.
struct FrameDataIsoTriangles {
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
    int _scatterPad;
    int4 visibleFaceIds;
    float4 detachedResidual;   // octahedral-snap residual quat (qx,qy,qz,qw)
    float4 detachedDepthAxis;  // isoDepthAxisModel(residual); .xyz used
    float4 scatterFbResolution; // framebuffer .xy for the conservative dilation (#1494)
};

// Must match f_peraxis_scatter's [[stage_in]] VertexOut (peraxis_scatter.metal).
struct VertexOut {
    float4 position [[position]];
    float4 color [[flat]];
    float depth [[flat]];
};

// Mirror of faceSpanCorner in v_peraxis_scatter_detached.glsl.
static inline float3 faceSpanCorner(int axis, float3 origin, float2 cornerSel) {
    if (axis == 0) return origin + float3(0.0, cornerSel.x, cornerSel.y); // X face: span y,z
    if (axis == 1) return origin + float3(cornerSel.x, 0.0, cornerSel.y); // Y face: span x,z
    return origin + float3(cornerSel.x, cornerSel.y, 0.0);                // Z face: span x,y
}

vertex VertexOut v_peraxis_scatter_detached(
    VertexIn in [[stage_in]],
    uint instanceId [[instance_id]],
    texture2d<float> triangleColors [[texture(0)]],
    texture2d<int> triangleDistances [[texture(1)]],
    constant GlobalConstants& globals [[buffer(1)]],
    constant FrameDataIsoTriangles& frameData [[buffer(3)]]
) {
    VertexOut out;
    const int2 canvasSize = int2(triangleColors.get_width(), triangleColors.get_height());
    const int cell = int(instanceId);
    const uint2 ij = uint2(uint(cell % canvasSize.x), uint(cell / canvasSize.x));

    const float4 color = triangleColors.read(ij);
    if (color.a < 0.1f) {
        out.position = float4(2.0, 2.0, 2.0, 1.0);
        out.color = float4(0.0);
        out.depth = 1.0;
        return out;
    }

    const int rawDist = triangleDistances.read(ij).r;
    const int rawDepth = rawDist >> 2;
    const int slot = rawDist & 3;
    const int faceId = frameData.visibleFaceIds[slot];
    const int axis = faceId >> 1;

    // Exact face-local recovery of the MODEL origin (mirror of the GLSL twin);
    // the camera iso baked into perAxisBase by the store cancels here.
    const int3 anchor = faceLocalAnchor(frameData.perAxisBase, canvasSize);
    const int2 inPlane = int2(ij) - faceLocalBase(axis, anchor, canvasSize);
    const int3 origin = faceOriginFromInPlane(faceId, inPlane, rawDepth);

    const float2 cornerSel = in.position + float2(0.5);
    const float3 modelCorner = faceSpanCorner(axis, float3(origin), cornerSel);
    const float2 isoModel = pos3DtoPos2DIsoRotated(modelCorner, frameData.detachedResidual);
    float4 clipCorner = frameData.mpMatrix * float4(isoModel, 1.0, 1.0);
    // Conservative screen-space coverage (#1494) — mirror of the GLSL twin. Grow
    // the quad outward along its two screen edge normals so a sub-pixel-thin
    // deformed rhombus still covers a fragment center (off-snap poses waffle
    // without it). Done in pre-y-flip clip space (mpMatrix is ortho, w == 1).
    const float2 fbRes = max(frameData.scatterFbResolution.xy, float2(1.0));
    const float2 ndcPerPx = float2(2.0) / fbRes;
    const float2 pxPerNdc = fbRes * 0.5;
    float3 eu, ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    float2 su = (frameData.mpMatrix * float4(pos3DtoPos2DIsoRotated(eu, frameData.detachedResidual), 0.0, 0.0)).xy * pxPerNdc;
    float2 sv = (frameData.mpMatrix * float4(pos3DtoPos2DIsoRotated(ev, frameData.detachedResidual), 0.0, 0.0)).xy * pxPerNdc;
    clipCorner.xy += scatterConservativeDilation(su, sv, sign(in.position), kScatterDilateMarginPx, ndcPerPx);
    clipCorner.y = -clipCorner.y;
    out.position = clipCorner;

    out.color = color;
    // Composite depth along the residual's model iso axis (#1475) — mirror of
    // v_peraxis_scatter_detached.glsl. rawDepth is the recovery key only; the
    // sort uses isoDepthAlongAxis(origin, detachedDepthAxis), *4 + slot encoded.
    const int residualDist =
        isoDepthAlongAxis(origin, frameData.detachedDepthAxis.xyz) * 4 + slot;
    out.depth = float(residualDist + frameData.distanceOffset - globals.kMinTriangleDistance) /
                float(globals.kMaxTriangleDistance - globals.kMinTriangleDistance);
    return out;
}

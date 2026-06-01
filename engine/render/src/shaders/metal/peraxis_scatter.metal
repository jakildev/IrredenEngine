// Project: Irreden Engine
// File: metal/peraxis_scatter.metal
// Smooth camera Z-yaw — T3 (#1310) forward-scatter composite (Metal mirror of
// v_/f_peraxis_scatter.glsl). Each instance is one per-axis canvas cell; the
// vertex stage recovers the face origin and projects its true deformed face
// quad, the fragment stage writes color + depth so the framebuffer depth test
// composites the three per-axis canvases.

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

// Shared with trixel_to_framebuffer.metal (buffer 3); the scatter reads the
// extra perAxisBase / visualYaw / visibleFaceIds the C++ FrameData appends.
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
};

struct VertexOut {
    float4 position [[position]];
    float4 color [[flat]];
    float depth [[flat]];
};

struct FragmentOut {
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

// In-plane corner of a face whose `origin` ALREADY sits at the face plane on
// the fixed axis (the store bakes the polarity via faceMicroPositionFixed6).
// Spans only the face's two in-plane world axes (X->y,z  Y->x,z  Z->x,y); re-
// adding the polarity offset double-shifts POS faces one cell past the plane —
// the #1310 back-face seam. Mirror of faceSpanCorner in v_peraxis_scatter.glsl.
static inline float3 faceSpanCorner(int axis, float3 origin, float2 cornerSel) {
    if (axis == 0) return origin + float3(0.0, cornerSel.x, cornerSel.y); // X face: span y,z
    if (axis == 1) return origin + float3(cornerSel.x, 0.0, cornerSel.y); // Y face: span x,z
    return origin + float3(cornerSel.x, cornerSel.y, 0.0);                // Z face: span x,y
}

vertex VertexOut v_peraxis_scatter(
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

    // Exact face-local recovery (#1310 fix) — mirror of v_peraxis_scatter.glsl.
    // The cell's in-plane coords + iso depth give the origin by one integer
    // subtraction: no 2cos(yaw)+1 inverse (which dropped compressed-axis faces
    // and went singular at +/-120 deg). anchor matches the stage 1/2 store.
    const int3 anchor = faceLocalAnchor(frameData.perAxisBase, canvasSize);
    const int2 inPlane = int2(ij) - faceLocalBase(axis, anchor, canvasSize);
    const float3 origin = float3(faceOriginFromInPlane(faceId, inPlane, rawDepth));

    const float2 cornerSel = in.position + float2(0.5);
    const float3 worldCorner = faceSpanCorner(axis, origin, cornerSel);
    const float2 cornerIso =
        float2(frameData.perAxisBase) + pos3DtoPos2DIsoYawed(worldCorner, frameData.visualYaw);

    float2 quadPos;
    quadPos.x = cornerIso.x / float(canvasSize.x) - 0.5f;
    quadPos.y = 0.5f - cornerIso.y / float(canvasSize.y);
    out.position = frameData.mpMatrix * float4(quadPos, 1.0, 1.0);
    out.position.y = -out.position.y;

    out.color = color;
    // Yaw-consistent composite depth (#1370) — mirror of v_peraxis_scatter.glsl.
    // `rawDepth` is the origin-recovery key (unchanged); re-derive the composite
    // depth from the recovered origin rotated by R_z(-visualYaw) so it matches
    // the YAWED screen projection and co-sorts with the SDF. Keeps the *4 + slot
    // encoding. Per-axis is residual-only -> cardinal fast path byte-identical.
    const float yc = cos(frameData.visualYaw);
    const float ys = sin(frameData.visualYaw);
    const float dvx = origin.x * yc + origin.y * ys;
    const float dvy = -origin.x * ys + origin.y * yc;
    const int yawedDist = roundHalfUp(dvx + dvy + origin.z) * 4 + slot;
    out.depth = float(yawedDist + frameData.distanceOffset - globals.kMinTriangleDistance) /
                float(globals.kMaxTriangleDistance - globals.kMinTriangleDistance);
    return out;
}

fragment FragmentOut f_peraxis_scatter(VertexOut in [[stage_in]]) {
    FragmentOut out;
    if (in.color.a < 0.1f) {
        discard_fragment();
    }
    out.color = in.color;
    out.depth = in.depth;
    return out;
}

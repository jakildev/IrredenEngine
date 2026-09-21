#include <metal_stdlib>
using namespace metal;
#include "ir_iso_common.metal"
#include "ir_sun_shadow_sample.metal"
#include "ir_source_face_lighting.metal"
struct VertexIn { float2 position [[attribute(0)]]; };
struct GlobalConstants { int kMinTriangleDistance; int kMaxTriangleDistance; };
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
    int scatterDebugMode;
    int4 visibleFaceIds;
    float4 _detachedResidualPad;
    float4 _detachedDepthAxisPad;
    float4 scatterFbResolution;
    int depthColorMode;
    float depthColorExtent;
    int anyPerTrixelPriority;
    int depthPriorityMode;
    int overflowMode;
    int trixelSampleLayout;
};
struct SourceFaceVertex {
    float4 position [[position]];
    float4 color [[flat]];
    uint priority [[flat]];
    float depth [[center_no_perspective]];
    float3 worldPosition [[center_no_perspective]];
    float3 worldNormal [[flat]];
    float4 directSunAndExposure [[flat]];
    float ao [[flat]];
    uint lightingMode [[flat]];
};
struct SourceFaceFragment { float4 color [[color(0)]]; float depth [[depth(any)]]; };
vertex SourceFaceVertex v_source_face_scatter(
    VertexIn in [[stage_in]], uint instanceId [[instance_id]],
    texture2d<float> triangleColors [[texture(0)]],
    constant FrameDataIsoTriangles& frameData [[buffer(3)]],
    device const SourceVoxelFaces& sourceFaces [[buffer(8)]],
    device const uint* sourceOrder [[buffer(25)]]) {
    const SourceVoxelFace face = sourceFaces.faces[sourceOrder[128u + instanceId * 3u + 1u]];
    const int faceId = int(face.centerAndFace.w);
    const int axis = faceId >> 1;
    float3 eu, ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    float3 corner = face.centerAndFace.xyz - float3(0.5);
    corner[axis] += float(faceId & 1);
    corner += eu * (in.position.x + 0.5) + ev * (in.position.y + 0.5);
    const float3 viewCorner = rotateByQuat(corner, frameData._detachedResidualPad);
    const float density = frameData.effectiveSubdivisionsForHover.x;
    const float2 iso = pos3DtoPos2DIsoYawed(viewCorner, 0.0) * density + float2(density - 1.0);
    const float2 extent = float2(triangleColors.get_width(), triangleColors.get_height());
    SourceFaceVertex out;
    out.position = frameData.mpMatrix * float4(iso.x / extent.x, -iso.y / extent.y, 0.0, 1.0);
    out.position.y = -out.position.y;
    out.depth = dot(viewCorner, float3(1.0)) * density * float(kDepthEncodeShift) *
        frameData.effectiveSubdivisionsForHover.y;
    const float3 modelNormal = faceOutwardNormal6(faceId);
    const float3 localCenter = face.centerAndFace.xyz + modelNormal * 0.5;
    out.worldPosition = face.worldCenterAndAO.xyz + rotateByQuat(
        viewCorner - rotateByQuat(localCenter, frameData._detachedResidualPad), frameData._detachedDepthAxisPad);
    out.worldNormal = rotateByQuat(rotateByQuat(modelNormal, frameData._detachedResidualPad), frameData._detachedDepthAxisPad);
    out.directSunAndExposure = face.directSunAndExposure;
    out.ao = face.worldCenterAndAO.w;
    out.lightingMode = face.owner.z;
    out.color = face.color;
    out.priority = decodePriority(face.owner.xy);
    return out;
}
fragment SourceFaceFragment f_source_face_scatter(
    SourceFaceVertex in [[stage_in]],
    constant FrameDataIsoTriangles& frameData [[buffer(3)]],
    constant GlobalConstants& globals [[buffer(1)]],
    constant FrameDataSun& sunFrameData [[buffer(29)]],
    device const uint* sunDepthBuf [[buffer(28)]]) {
    if (in.color.a < 0.1) discard_fragment();
    const int tier = max(frameData.depthPriorityMode, int(in.priority));
    float depth = in.depth + float(frameData.distanceOffset);
    if (tier > 0) depth = clamp(in.depth + float(depthForegroundTierCenter(globals.kMinTriangleDistance, tier)),
        float(depthForegroundTierLo(globals.kMinTriangleDistance, tier)),
        float(depthForegroundTierHi(globals.kMinTriangleDistance, tier)));
    else depth = max(depth, float(globals.kMinTriangleDistance + kDepthForegroundBandWidth + 1));
    SourceFaceFragment out;
    out.depth = (depth - float(globals.kMinTriangleDistance)) /
        float(globals.kMaxTriangleDistance - globals.kMinTriangleDistance);
    out.color = in.color;
    if (in.lightingMode != kSourceLightingBaked) {
        const float visibility = worldSurfaceSunShadowFactor(in.worldPosition, in.worldNormal,
            pos3DtoDistance(in.worldPosition), frameData._detachedDepthAxisPad, sunFrameData, sunDepthBuf);
        out.color = sourceFaceLitColor(in.color, in.directSunAndExposure, in.ao,
            in.lightingMode, visibility);
    }
    return out;
}

#version 450 core
#include "ir_iso_common.glsl"
layout(location = 0) in vec2 aPos;
layout(binding = 0) uniform sampler2D triangleColors;
layout(std430, binding = 8) readonly buffer SourceVoxelFaces {
    uint sourceIndexCount; uint sourceFaceCount; uint sourcePadding[6];
    SourceVoxelFace sourceFaces[];
};
layout (std140, binding = 3) uniform FrameDataIsoTriangles {
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
    int anyPerTrixelPriority;
    int depthPriorityMode;
    int overflowMode;
    int trixelSampleLayout;
};
layout(std430, binding = 25) readonly buffer SourceFaceOrder { uint sourceOrder[]; };
flat out vec4 faceColor;
flat out uint facePriority;
out float faceDepth;
noperspective out vec3 faceWorldPosition;
flat out vec3 faceWorldNormal;
flat out vec4 faceDirectSunAndExposure;
flat out float faceAO;
flat out uint faceLightingMode;
void main() {
    const SourceVoxelFace face = sourceFaces[sourceOrder[128u + uint(gl_InstanceID) * 3u + 1u]];
    const int faceId = int(face.centerAndFace.w);
    const int axis = faceId >> 1;
    vec3 eu, ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    vec3 corner = face.centerAndFace.xyz - vec3(0.5);
    corner[axis] += float(faceId & 1);
    corner += eu * (aPos.x + 0.5) + ev * (aPos.y + 0.5);
    const vec3 viewCorner = rotateByQuat(corner, _detachedResidualPad);
    const float density = effectiveSubdivisionsForHover.x;
    const vec2 iso = pos3DtoPos2DIsoYawed(viewCorner, 0.0) * density + vec2(density - 1.0);
    const vec2 extent = vec2(textureSize(triangleColors, 0));
    gl_Position = mpMatrix * vec4(iso.x / extent.x, -iso.y / extent.y, 0.0, 1.0);
    faceDepth = dot(viewCorner, vec3(1.0)) * density * float(kDepthEncodeShift) *
        effectiveSubdivisionsForHover.y;
    const vec3 modelNormal = faceOutwardNormal6(faceId);
    const vec3 localCenter = face.centerAndFace.xyz + modelNormal * 0.5;
    faceWorldPosition = face.worldCenterAndAO.xyz + rotateByQuat(
        viewCorner - rotateByQuat(localCenter, _detachedResidualPad), _detachedDepthAxisPad);
    faceWorldNormal = rotateByQuat(rotateByQuat(modelNormal, _detachedResidualPad), _detachedDepthAxisPad);
    faceDirectSunAndExposure = face.directSunAndExposure;
    faceAO = face.worldCenterAndAO.w;
    faceLightingMode = face.owner.z;
    faceColor = face.color;
    facePriority = decodePriority(face.owner.xy);
}

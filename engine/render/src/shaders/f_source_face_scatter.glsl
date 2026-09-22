#version 450 core
#include "ir_iso_common.glsl"
#include "ir_sun_shadow_sample.glsl"
#include "ir_source_face_lighting.glsl"
layout(std140, binding = 1) uniform GlobalConstants {
    int kMinTriangleDistance; int kMaxTriangleDistance;
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
    vec4 viewToWorldRotation;
    vec4 scatterFbResolution;
    int depthColorMode;
    float depthColorExtent;
    int anyPerTrixelPriority;
    int depthPriorityMode;
    int overflowMode;
    int trixelSampleLayout;
};
flat in vec4 faceColor;
flat in uint facePriority;
in float faceDepth;
noperspective in vec3 faceWorldPosition;
flat in vec3 faceWorldNormal;
flat in vec4 faceDirectSunAndExposure;
flat in float faceAO;
flat in uint faceLightingMode;
layout(location = 0) out vec4 FragColor;
void main() {
    if (faceColor.a < 0.1) discard;
    const int tier = max(depthPriorityMode, int(facePriority));
    float depth = faceDepth + float(distanceOffset);
    if (tier > 0) depth = clamp(faceDepth + float(depthForegroundTierCenter(kMinTriangleDistance, tier)),
        float(depthForegroundTierLo(kMinTriangleDistance, tier)),
        float(depthForegroundTierHi(kMinTriangleDistance, tier)));
    else depth = max(depth, float(kMinTriangleDistance + kDepthForegroundBandWidth + 1));
    gl_FragDepth = (depth - float(kMinTriangleDistance)) /
        float(kMaxTriangleDistance - kMinTriangleDistance);
    FragColor = faceColor;
    if (faceLightingMode != kSourceLightingBaked) {
        const float visibility = worldSurfaceSunShadowFactor(faceWorldPosition, faceWorldNormal,
            pos3DtoDistance(faceWorldPosition), viewToWorldRotation);
        FragColor = sourceFaceLitColor(faceColor, faceDirectSunAndExposure, faceAO,
            faceLightingMode, visibility);
    }
}

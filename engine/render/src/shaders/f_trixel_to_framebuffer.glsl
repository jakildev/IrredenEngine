/*
 * Project: Irreden Engine
 * File: f_trixel_to_framebuffer.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 460 core

#include "ir_iso_common.glsl"

in vec2 TexCoords;

layout (binding = 0) uniform sampler2D triangleColors;
layout (binding = 1) uniform isampler2D  triangleDistances;
layout (binding = 2) uniform usampler2D triangleEntityIds;

layout(std140, binding = 1) uniform GlobalConstants {
    uniform int kMinTriangleDistance;
    uniform int kMaxTriangleDistance;
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
};

layout(std430, binding = 14) buffer HoveredEntityIdBuffer {
    uvec2 hoveredEntityId;
    float hoveredDepth;
};

out vec4 FragColor;

float normalizeDistance(int dist) {
    // return float(dist) / float(kMaxTriangleDistance);
    return float(dist - kMinTriangleDistance) / float(kMaxTriangleDistance - kMinTriangleDistance);
}

void main() {
    ivec2 textureSize = textureSize(triangleColors, 0);
    ivec2 z1 = trixelOriginOffsetZ1(textureSize);
    vec2 origin = TexCoords * vec2(textureSize);
    int originModifier = trixelOriginModifier(z1, canvasOffset);
    origin = trixelFramebufferSamplePosition(origin, originModifier);

    vec4 color = textureLod(triangleColors, origin / textureSize, 0);
    int rawDist = textureLod(triangleDistances, origin / textureSize, 0).r;
    float depth = normalizeDistance(rawDist + distanceOffset);
    // Match voxel-to-trixel write: texture coord = trixelOriginOffsetZ1 + canvasOffset + worldIndex
    // canvasOffset is already scaled by subdivisions in smooth mode (CPU side)
    // mouseHoveredTriangleIndex is base space; scale to subdivided space for comparison
    int subdivisions = max(int(effectiveSubdivisionsForHover.x), 1);
    vec2 hoveredPosition =
        mouseHoveredTriangleIndex * float(subdivisions) +
        vec2(trixelOriginOffsetZ1(textureSize)) +
        canvasOffset;
    ivec2 originIndex = ivec2(floor(origin));
    ivec2 hoveredIndex = ivec2(floor(hoveredPosition));
    bool isMouseHovered = all(equal(hoveredIndex, originIndex));
    if (isMouseHovered) {
        if (color.a >= 0.1 && depth <= hoveredDepth) {
            uvec2 entityId = textureLod(triangleEntityIds, origin / vec2(textureSize), 0).rg;
            if (entityId != uvec2(0u)) {
                hoveredEntityId = entityId;
                hoveredDepth = depth;
            }
        }
        if (showHoverHighlight > 0.0) {
            color = vec4(1.0, 0.0, 0.0, 1.0);
            depth = 0.0;
        }
    }
    if(color.a < 0.1) {
		discard;
	}
	FragColor = color;
    gl_FragDepth = depth;
}
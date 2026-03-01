/*
 * Project: Irreden Engine
 * File: f_trixel_to_framebuffer.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 460 core

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
    vec2 _padding0;
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

ivec2 trixelOriginOffsetX1(ivec2 trixelCanvasSize) {
    return ivec2(trixelCanvasSize) / ivec2(2);
}

ivec2 trixelOriginOffsetZ1(ivec2 trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, -1);
}

void main() {
    ivec2 textureSize = textureSize(triangleColors, 0);
    ivec2 z1 = trixelOriginOffsetZ1(textureSize);
    vec2 canvasOffsetFloored = floor(canvasOffset);
    vec2 origin = TexCoords * vec2(textureSize);
    vec2 originFlooredComp = floor(origin);
    vec2 fractComp = fract(origin);
    int originModifier = (z1.x + z1.y + int(canvasOffsetFloored.x) + int(canvasOffsetFloored.y)) & 1;
    // int originModifier = (z1.x + z1.y) & 1;

    // See IRMath::pos2DIsoToTriangleIndex
    if(mod(originFlooredComp.x + originFlooredComp.y + originModifier, 2.0) >= 1) {
        if(fractComp.y < fractComp.x) {
            origin = vec2(origin.x, origin.y - 1);
        }
    }
    else {
        if(fractComp.y < 1 - fractComp.x) {
            origin = vec2(origin.x, origin.y - 1);
        }
    }

    // // Now wrap the edge cases
    // if(origin.y == textureSize.y) {
    //     origin.y = 0;
    // }

    vec4 color = textureLod(triangleColors, origin / textureSize, 0);
    float depth = normalizeDistance(
        textureLod(triangleDistances, origin / textureSize, 0).r
    );
    // Match voxel-to-trixel write: texture coord = trixelOriginOffsetZ1 + canvasOffset + worldIndex
    vec2 hoveredPosition =
        mouseHoveredTriangleIndex +
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
        color = vec4(1.0, 0.0, 0.0, 1.0);
        depth = 0.0;
    }
    if(color.a < 0.1) {
		discard;
	}
	FragColor = color;
    gl_FragDepth = depth;
}
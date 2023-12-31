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
    ivec2 screenSize = textureSize;
    vec2 origin = TexCoords * screenSize;
    vec2 originFlooredComp = floor(origin);
    vec2 fractComp = fract(origin);
    int originModifier = (
        trixelOriginOffsetZ1(textureSize).x +
        trixelOriginOffsetZ1(textureSize).y +
        int(floor(canvasOffset.x)) +
        int(floor(canvasOffset.y))
   ) % 2;

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
   if(floor(
            mouseHoveredTriangleIndex +
            trixelOriginOffsetX1(textureSize) +
            canvasOffset
        ) ==
        floor(origin))
    {
        // color = mix(color, vec4(0, 0, 0, 1), 0.3);
        color = vec4(255, 0, 0, 1);
        depth = 0.0;
    }
    if(color.a < 0.1) {
		discard;
	}
    // if(depth >= gl_FragCoord.z) {
    //     discard;
    // }
	FragColor = color;
    gl_FragDepth = depth;
}
/*
 * Project: Irreden Engine
 * File: f_trixel_to_framebuffer.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 450 core

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
    // The scatter UBO tail (consumed only by v_/f_peraxis_scatter). Declared
    // here only to reach depthPriorityMode at offset 204; the gather reads none
    // of these but the std140 layout must match the shared C++ struct
    // (FrameDataTrixelToFramebuffer, ir_render_types.hpp).
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
    // Two-tier composite depth partition (#1958): 0 = world content (clamped out
    // of the reserved near band), != 0 = foreground priority (pinned into it).
    int depthPriorityMode;
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
    // effectiveSubdivisionsForHover.y carries the per-canvas depth rescale
    // (effSub / cubeSub) for world-placed DETACHED canvases: their model-frame
    // rawDist was written at the canvas's own (possibly #1570-D2-capped)
    // subdivision, so it must be lifted into the shared framebuffer depth units
    // (worldDepth × effSub × 4) before the world iso-depth offset is added — the
    // #1624 world-placed depth fix. 0 (world/overlay canvases, zero-init) → 1.0,
    // i.e. the byte-identical fast path.
    float depthScale = effectiveSubdivisionsForHover.y;
    if (depthScale <= 0.0) depthScale = 1.0;
    int enc = int(round(float(rawDist) * depthScale)) + distanceOffset;
    // Two-tier composite depth partition (#1958). The most-negative
    // kDepthForegroundBandWidth codes of [kMin, kMax] are reserved for
    // foreground-priority detached solids. More-negative enc = nearer (GL_LESS +
    // normalizeDistance), so this near slice is the priority partition.
    // - depthPriorityMode != 0 (a world-placed C_EntityCanvas with depthPriority_
    //   set): pin the canvas's model-frame local iso-depth INTO the band so it is
    //   unconditionally nearer than any world fragment regardless of world extent.
    //   Clamping the band edges makes a pathologically deep solid saturate
    //   (graceful degradation) instead of escaping into the world range.
    // - depthPriorityMode == 0 (the main world gather + every non-priority
    //   composite): clamp world content OUT of the band. A no-op for all in-budget
    //   content (enc >> foregroundCeil), so the cardinal fast path is
    //   byte-identical; far world content saturates against the boundary (loses
    //   depth resolution) rather than letting a background fragment beat a
    //   priority solid.
    int foregroundCeil = kMinTriangleDistance + kDepthForegroundBandWidth;
    if (depthPriorityMode != 0) {
        enc = clamp(enc, kMinTriangleDistance, foregroundCeil);
    } else {
        enc = max(enc, foregroundCeil + 1);
    }
    float depth = normalizeDistance(enc);
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
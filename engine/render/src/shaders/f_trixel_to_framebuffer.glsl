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
    // Offset 200. 0 = no per-trixel-priority voxel in this canvas, so the tier
    // read of triangleEntityIds is skipped; != 0 = read + decode the tier.
    int anyPerTrixelPriority;
    // Composite depth tier for this draw: 0 = world content (clamped out of the
    // reserved near band), != 0 = foreground priority (pinned into it).
    int depthPriorityMode;
    int overflowMode;
    int trixelSampleLayout;
};

layout(std430, binding = 14) buffer HoveredEntityIdBuffer {
    uvec2 hoveredEntityId;
    float hoveredDepth;
};

out vec4 FragColor;

float normalizeDistance(int dist) {
    return float(dist - kMinTriangleDistance) / float(kMaxTriangleDistance - kMinTriangleDistance);
}

void main() {
    ivec2 textureSize = textureSize(triangleColors, 0);
    ivec2 z1 = trixelOriginOffsetZ1(textureSize);
    // Rectangular color/depth/tier reads use the raw interpolated canvas
    // position. Hover uses the world-lattice mapping; local display triangles
    // instead select cells in the private canvas basis.
    vec2 originRaw = TexCoords * vec2(textureSize);
    int originModifier = trixelOriginModifier(z1, canvasOffset);
    vec2 originShifted = trixelFramebufferSamplePosition(originRaw, originModifier);

    vec2 displayOrigin = originRaw;
    if (trixelSampleLayout == 1) {
        // The triangular cell footprint is centered one row below its
        // stored index; offset the query to preserve the voxel origin.
        displayOrigin = trixelFramebufferSamplePosition(
            originRaw + vec2(0.0, 1.0), (z1.x + z1.y) & 1);
        if (any(lessThan(displayOrigin, vec2(0.0))) ||
            any(greaterThanEqual(displayOrigin, vec2(textureSize)))) discard;
    }
    vec4 color = textureLod(triangleColors, displayOrigin / textureSize, 0);
    int rawDist = textureLod(triangleDistances, displayOrigin / textureSize, 0).r;
    // effectiveSubdivisionsForHover.y carries the per-canvas depth rescale
    // (effSub / cubeSub) for world-placed DETACHED canvases: their model-frame
    // rawDist was written at the canvas's own (possibly capped) subdivision, so
    // it must be lifted into the shared framebuffer depth units
    // (worldDepth × effSub × 8) before the world iso-depth offset is added.
    // 0 (world/overlay canvases, zero-init) → 1.0, i.e. no rescale.
    float depthScale = effectiveSubdivisionsForHover.y;
    if (depthScale <= 0.0) depthScale = 1.0;
    // roundHalfUp, not hardware round(): a fractional depthScale (effSub /
    // cubeSub) lands odd rawDist values on exact .5 ties, where GLSL round()
    // is implementation-defined and diverges from the Metal twin.
    int base = roundHalfUp(float(rawDist) * depthScale);
    // Match voxel-to-trixel write: texture coord = trixelOriginOffsetZ1 + canvasOffset + worldIndex
    // canvasOffset is already scaled by subdivisions in smooth mode (CPU side)
    // mouseHoveredTriangleIndex is base space; scale to subdivided space for comparison
    int subdivisions = max(int(effectiveSubdivisionsForHover.x), 1);
    vec2 hoveredPosition =
        mouseHoveredTriangleIndex * float(subdivisions) +
        vec2(trixelOriginOffsetZ1(textureSize)) +
        canvasOffset;
    ivec2 originIndex = ivec2(floor(originShifted));
    ivec2 hoveredIndex = ivec2(floor(hoveredPosition));
    bool isMouseHovered = all(equal(hoveredIndex, originIndex));
    // Priority and color/depth must select the same stored cell. Canvases
    // without prioritized voxels avoid the extra entity-id fetch.
    int tier = depthPriorityMode;
    if (anyPerTrixelPriority != 0) {
        uvec2 sampleEntityId = textureLod(triangleEntityIds, displayOrigin / vec2(textureSize), 0).rg;
        // Resolve the tier: the higher of this draw's per-entity tier
        // (depthPriorityMode, C_EntityCanvas::depthPriority_) and the
        // per-voxel tier authored into the id carrier.
        tier = max(depthPriorityMode, int(decodePriority(sampleEntityId)));
    }
    int foregroundCeil = kMinTriangleDistance + kDepthForegroundBandWidth;
    int enc;
    if (tier == 0) {
        // World content: clamp OUT of the reserved near band. A no-op for every
        // in-budget fragment (base + distanceOffset >> foregroundCeil); far world
        // content saturates against the boundary rather than letting a background
        // fragment beat a priority solid.
        enc = max(base + distanceOffset, foregroundCeil + 1);
    } else {
        // Foreground tier: center this fragment's model-frame local iso-depth in
        // the resolved tier and pin it into the tier's disjoint sub-range. A
        // higher tier is a strictly more-negative (nearer) sub-range, so it wins
        // unconditionally against every lower tier and all world content,
        // independent of world extent. A pathologically deep solid saturates
        // against its tier edge (graceful degradation) instead of escaping. The
        // per-draw distanceOffset (world placement) is intentionally dropped here
        // — priority OVERRIDES world depth ordering.
        enc = clamp(base + depthForegroundTierCenter(kMinTriangleDistance, tier),
                    depthForegroundTierLo(kMinTriangleDistance, tier),
                    depthForegroundTierHi(kMinTriangleDistance, tier));
    }
    float depth = normalizeDistance(enc);
    if (isMouseHovered) {
        if (color.a >= 0.1 && depth <= hoveredDepth) {
            // Strip the per-trixel priority carrier so a prioritized fragment
            // reports its true picked id. The hover read uses the shifted
            // coordinate (kept in lockstep with CPU mouseTrixelPositionWorld),
            // distinct from the originRaw tier read.
            uvec2 entityId = decodeEntityId(
                textureLod(triangleEntityIds, originShifted / vec2(textureSize), 0).rg);
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

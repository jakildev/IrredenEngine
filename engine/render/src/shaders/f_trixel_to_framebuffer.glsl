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
    // No-priority perf fast-path (#2155): 0 = no per-trixel-priority voxel in this
    // canvas, so the finalization path skips the triangleEntityIds decode read on
    // non-hovered fragments; != 0 = read + decode as before. Repurposes the former
    // _depthColorPad0 slot at offset 200 (4-byte scalar, layout-identical).
    int anyPerTrixelPriority;
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
    // Parity-row shift applied to ALL reads on GL (color/depth/id); GL-only —
    // Metal reads color/depth from the raw origin because its flipped raster
    // (top-left target vs GL's bottom-left) already lands on the right row. See
    // trixelFramebufferSamplePosition in ir_iso_common.glsl; #442,
    // docs/design/trixel-parity-shift-442-investigation.md.
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
    int base = int(round(float(rawDist) * depthScale));
    // Hover state, computed BEFORE the (now-conditional) entity-id read (#2155).
    // It needs only `origin` + the hover uniforms — no texture fetch — so hoisting
    // it above the read is inert, and it lets the read be gated on hover too.
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
    // Per-trixel priority tiers (#1960; generalizes #1958's two-tier partition).
    // The finalization path only needs this fragment's stored entity id when some
    // voxel in the canvas carries a per-trixel priority (anyPerTrixelPriority) OR
    // this is the hovered fragment (picking still needs the id below — the SAME
    // sample feeds both). On the default no-priority, non-hovered path the read is
    // skipped (#2155 fast path): decodePriority of an unread id would be 0, so
    // tier == depthPriorityMode and the output is byte-identical.
    uvec2 rawEntityId = uvec2(0u);
    int tier = depthPriorityMode;
    if (anyPerTrixelPriority != 0 || isMouseHovered) {
        rawEntityId = textureLod(triangleEntityIds, origin / vec2(textureSize), 0).rg;
        // Resolve the tier: the higher of this draw's per-entity tier
        // (depthPriorityMode, #1958's C_EntityCanvas::depthPriority_) and the
        // per-voxel tier authored into the id carrier.
        tier = max(depthPriorityMode, int(decodePriority(rawEntityId)));
    }
    int foregroundCeil = kMinTriangleDistance + kDepthForegroundBandWidth;
    int enc;
    if (tier == 0) {
        // World content: clamp OUT of the reserved near band. A no-op for every
        // in-budget fragment (base + distanceOffset >> foregroundCeil), so the
        // cardinal fast path stays byte-identical to #1958 master; far world
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
    // rawEntityId is guaranteed read whenever isMouseHovered is true (the
    // conditional read carries the `|| isMouseHovered` disjunct), so hover-pick
    // decode stays correct even when the priority fast-path skipped the read.
    if (isMouseHovered) {
        if (color.a >= 0.1 && depth <= hoveredDepth) {
            // Strip the per-trixel priority carrier so a prioritized fragment
            // reports its true picked id (#1960 masking-trap discipline). Reuses
            // the sample taken above for the tier resolve.
            uvec2 entityId = decodeEntityId(rawEntityId);
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
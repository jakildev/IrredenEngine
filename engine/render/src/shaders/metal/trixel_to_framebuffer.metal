#include <metal_stdlib>
using namespace metal;

#include "ir_iso_common.metal"

struct VertexIn {
    float2 position [[attribute(0)]];
};

struct GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

struct FrameDataIsoTriangles {
    float4x4 mpMatrix;
    float2 zoomLevel;
    float2 canvasOffset;
    float2 textureOffset;
    float2 mouseHoveredTriangleIndex;
    float2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    // Scatter UBO tail (consumed only by peraxis_scatter). Declared here only to
    // reach depthPriorityMode at offset 204 — the gather reads none of these but
    // the struct must mirror the shared C++ FrameDataTrixelToFramebuffer layout.
    int2 perAxisBase;
    float visualYaw;
    int scatterDebugMode;
    int4 visibleFaceIds;
    float4 _detachedResidualPad;
    float4 _detachedDepthAxisPad;
    float4 scatterFbResolution;
    int depthColorMode;
    float depthColorExtent;
    // No-priority perf fast-path (#2155): 0 = no per-trixel-priority voxel in this
    // canvas, so the tier-decode triangleEntityIds read is skipped; != 0 = read +
    // decode as before. Repurposes the former _depthColorPad0 slot at offset 200
    // (4-byte scalar, layout-identical). Twin of f_trixel_to_framebuffer.glsl.
    int anyPerTrixelPriority;
    // Two-tier composite depth partition (#1958): 0 = world content (clamped out
    // of the reserved near band), != 0 = foreground priority (pinned into it).
    int depthPriorityMode;
};

// SSBO populated by the fragment shader when the mouse hovers over a
// non-transparent trixel that wins the depth test. CPU side is
// `HoveredEntityIdBuffer` (slot 14, kBufferIndex_HoveredEntityId);
// readback layout matches the C++ `HoveredEntityIdLayout` in
// `ir_render_types.hpp` and the GLSL std430 buffer in
// `f_trixel_to_framebuffer.glsl`.
struct HoveredEntityIdBuffer {
    uint2 hoveredEntityId;
    float hoveredDepth;
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoords;
};

struct FragmentOut {
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

float normalizeDistance(int dist, constant GlobalConstants& globals) {
    return float(dist - globals.kMinTriangleDistance) /
           float(globals.kMaxTriangleDistance - globals.kMinTriangleDistance);
}

vertex VertexOut v_trixel_to_framebuffer(
    VertexIn in [[stage_in]],
    texture2d<float> triangleColors [[texture(0)]],
    constant FrameDataIsoTriangles& frameData [[buffer(3)]]
) {
    VertexOut out;
    const float2 textureSize = float2(triangleColors.get_width(), triangleColors.get_height());
    out.position = frameData.mpMatrix * float4(in.position, 1.0, 1.0);
    out.position.y = -out.position.y;
    out.texCoords =
        float2(in.position.x, -in.position.y) + 0.5 + (frameData.textureOffset / textureSize);
    return out;
}

fragment FragmentOut f_trixel_to_framebuffer(
    VertexOut in [[stage_in]],
    texture2d<float> triangleColors [[texture(0)]],
    texture2d<int> triangleDistances [[texture(1)]],
    texture2d<uint> triangleEntityIds [[texture(2)]],
    constant GlobalConstants& globals [[buffer(1)]],
    constant FrameDataIsoTriangles& frameData [[buffer(3)]],
    device HoveredEntityIdBuffer& hovered [[buffer(14)]]
) {
    FragmentOut out;

    const float2 textureSize = float2(triangleColors.get_width(), triangleColors.get_height());
    const int2 z1 = trixelOriginOffsetZ1(int2(textureSize));

    // Color / depth read at the RAW interpolated canvas position: Metal's negated
    // clip-Y (top-left target vs GL's bottom-left) already lands the raw sample on
    // the correct trixel row, so — unlike GL — no parity-row shift is applied here.
    // The shifted index IS still computed (`originShifted` below) and used for
    // hover entity-id readback, so it stays in lockstep with CPU-side
    // `mouseTrixelPositionWorld()` (same `pos2DIsoToTriangleIndex` formula). See
    // trixelFramebufferSamplePosition in ir_iso_common.metal; #442,
    // docs/design/trixel-parity-shift-442-investigation.md.
    const float2 originRaw = in.texCoords * textureSize;
    const int originModifier = trixelOriginModifier(z1, frameData.canvasOffset);
    const float2 originShifted =
        trixelFramebufferSamplePosition(originRaw, originModifier);

    const uint2 sampleCoord = trixelCanvasReadCoord(originRaw, textureSize);
    const uint2 hoverCoord = trixelCanvasReadCoord(originShifted, textureSize);

    float4 color = triangleColors.read(sampleCoord);
    const int rawDist = triangleDistances.read(sampleCoord).r;
    // effectiveSubdivisionsForHover.y carries the per-canvas depth rescale
    // (effSub / cubeSub) for world-placed DETACHED canvases — see
    // f_trixel_to_framebuffer.glsl (#1624 world-placed depth fix). 0 → 1.0
    // (the byte-identical world/overlay fast path).
    float depthScale = frameData.effectiveSubdivisionsForHover.y;
    if (depthScale <= 0.0f) depthScale = 1.0f;
    // roundHalfUp, not hardware round(): a fractional depthScale (effSub /
    // cubeSub) lands odd rawDist values on exact .5 ties, where the GLSL
    // twin's round() is implementation-defined; both twins share roundHalfUp.
    int base = roundHalfUp(float(rawDist) * depthScale);
    // Per-trixel priority tiers (#1960) — twin of f_trixel_to_framebuffer.glsl.
    // No-priority perf fast-path (#2155): read this fragment's entity id (at the
    // SAME texel its color/depth came from — sampleCoord, the raw position) only
    // when the canvas carries a per-trixel priority. When it doesn't,
    // decodePriority of an unread id would be 0, so tier == depthPriorityMode and
    // the output is byte-identical. Unlike the GLSL twin this read never feeds
    // picking (the hover read below uses the shifted hoverCoord and is left
    // untouched + already gated on isMouseHovered), so no `|| isMouseHovered`
    // disjunct is needed here.
    int tier = frameData.depthPriorityMode;
    if (frameData.anyPerTrixelPriority != 0) {
        const uint2 sampleEntityId = triangleEntityIds.read(sampleCoord).rg;
        tier = max(frameData.depthPriorityMode, int(decodePriority(sampleEntityId)));
    }
    const int foregroundCeil = globals.kMinTriangleDistance + kDepthForegroundBandWidth;
    int enc;
    if (tier == 0) {
        // World content: clamp OUT of the reserved band (no-op for in-budget
        // content → byte-identical to #1958 master).
        enc = max(base + frameData.distanceOffset, foregroundCeil + 1);
    } else {
        // Foreground tier: center the model-frame local iso-depth in the resolved
        // tier and pin into its disjoint sub-range (more-negative = nearer, so a
        // higher tier wins unconditionally; saturates gracefully on overflow). The
        // per-draw distanceOffset (world placement) is dropped — priority overrides
        // world depth ordering.
        enc = clamp(base + depthForegroundTierCenter(globals.kMinTriangleDistance, tier),
                    depthForegroundTierLo(globals.kMinTriangleDistance, tier),
                    depthForegroundTierHi(globals.kMinTriangleDistance, tier));
    }
    float depth = normalizeDistance(enc, globals);

    const int subdivisions = max(int(frameData.effectiveSubdivisionsForHover.x), 1);
    const float2 hoveredPosition =
        frameData.mouseHoveredTriangleIndex * float(subdivisions) +
        float2(z1) +
        frameData.canvasOffset;
    const int2 originIndex = int2(floor(originShifted));
    const int2 hoveredIndex = int2(floor(hoveredPosition));
    const bool isMouseHovered = all(hoveredIndex == originIndex);
    if (isMouseHovered) {
        if (color.a >= 0.1f && depth <= hovered.hoveredDepth) {
            // Strip the per-trixel priority carrier so picking reports the true id
            // (#1960). The hover read uses the shifted hoverCoord (kept in lockstep
            // with CPU mouseTrixelPositionWorld), distinct from the sampleCoord
            // tier read above.
            const uint2 entityId = decodeEntityId(triangleEntityIds.read(hoverCoord).rg);
            if (any(entityId != uint2(0u))) {
                hovered.hoveredEntityId = entityId;
                hovered.hoveredDepth = depth;
            }
        }
        if (frameData.showHoverHighlight > 0.0f) {
            color = float4(1.0f, 0.0f, 0.0f, 1.0f);
            depth = 0.0f;
        }
    }

    if (color.a < 0.1f) {
        discard_fragment();
    }

    out.color = color;
    out.depth = depth;
    return out;
}

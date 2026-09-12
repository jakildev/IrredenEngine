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
    // Offset 200. 0 = no per-trixel-priority voxel in this canvas, so the tier
    // read of triangleEntityIds is skipped; != 0 = read + decode the tier.
    int anyPerTrixelPriority;
    // Composite depth tier for this draw: 0 = world content (clamped out of the
    // reserved near band), != 0 = foreground priority (pinned into it).
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

    // Color / depth read at the RAW interpolated canvas position — the raw
    // sample already lands on the correct trixel row (same convention as the
    // GLSL twin). The parity-row shift (trixelFramebufferSamplePosition) is
    // applied ONLY to the hover coordinate (`originShifted`), used for hover
    // entity-id readback, so it stays in lockstep with CPU-side
    // `mouseTrixelPositionWorld()` (same `pos2DIsoToTriangleIndex` formula).
    // Before editing, read docs/design/trixel-parity-shift-442-investigation.md.
    const float2 originRaw = in.texCoords * textureSize;
    const int originModifier = trixelOriginModifier(z1, frameData.canvasOffset);
    const float2 originShifted =
        trixelFramebufferSamplePosition(originRaw, originModifier);

    const uint2 sampleCoord = trixelCanvasReadCoord(originRaw, textureSize);
    const uint2 hoverCoord = trixelCanvasReadCoord(originShifted, textureSize);

    float4 color = triangleColors.read(sampleCoord);
    const int rawDist = triangleDistances.read(sampleCoord).r;
    // effectiveSubdivisionsForHover.y carries the per-canvas depth rescale
    // (effSub / cubeSub) for world-placed DETACHED canvases, lifting the
    // model-frame rawDist into the shared framebuffer depth units before the
    // world iso-depth offset is added. 0 (world/overlay canvases) → 1.0.
    float depthScale = frameData.effectiveSubdivisionsForHover.y;
    if (depthScale <= 0.0f) depthScale = 1.0f;
    // roundHalfUp, not hardware round(): a fractional depthScale (effSub /
    // cubeSub) lands odd rawDist values on exact .5 ties, where the GLSL
    // twin's round() is implementation-defined; both twins share roundHalfUp.
    int base = roundHalfUp(float(rawDist) * depthScale);
    // Per-trixel priority tiers — twin of f_trixel_to_framebuffer.glsl. Read
    // this fragment's entity id (at the SAME texel its color/depth came from —
    // sampleCoord, the raw position) only when the canvas carries a per-trixel
    // priority. When it doesn't, decodePriority of an unread id would be 0, so
    // tier == depthPriorityMode and the output is identical. This read never
    // feeds picking (the hover read uses the shifted hoverCoord and is gated on
    // isMouseHovered separately), so no `|| isMouseHovered` disjunct is needed.
    // So a fragment that is BOTH prioritized and hovered reads
    // triangleEntityIds TWICE — once at sampleCoord, once at hoverCoord. The
    // two reads want different texels, so the pair is not redundant: one shared
    // fetch has to pick a single coord, and either choice reintroduces a
    // parity-shifted read on the path that needs the other.
    int tier = frameData.depthPriorityMode;
    if (frameData.anyPerTrixelPriority != 0) {
        const uint2 sampleEntityId = triangleEntityIds.read(sampleCoord).rg;
        tier = max(frameData.depthPriorityMode, int(decodePriority(sampleEntityId)));
    }
    const int foregroundCeil = globals.kMinTriangleDistance + kDepthForegroundBandWidth;
    int enc;
    if (tier == 0) {
        // World content: clamp OUT of the reserved band (a no-op for in-budget
        // content).
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
            // Strip the per-trixel priority carrier so picking reports the true
            // id. The hover read uses the shifted hoverCoord (kept in lockstep
            // with CPU mouseTrixelPositionWorld), distinct from the sampleCoord
            // tier read.
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

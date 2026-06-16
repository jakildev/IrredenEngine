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

    // Color / depth read at the RAW interpolated canvas position — pre-#394
    // Metal did this and rendered cleanly. Applying the GLSL-style
    // `trixelFramebufferSamplePosition` row shift here introduced 1-pixel
    // sawtooth notches along every iso diagonal under Metal regardless of
    // shift sign or parity-branch swap. The shifted index is still computed
    // and used for hover entity-id readback so it stays in lockstep with
    // CPU-side `mouseTrixelPositionWorld()` (which routes through the same
    // `pos2DIsoToTriangleIndex` formula).
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
    float depth = normalizeDistance(
        int(round(float(rawDist) * depthScale)) + frameData.distanceOffset, globals);

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
            const uint2 entityId = triangleEntityIds.read(hoverCoord).rg;
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

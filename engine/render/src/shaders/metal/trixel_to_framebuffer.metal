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
    constexpr sampler triangleSampler(coord::normalized, address::clamp_to_edge, filter::nearest);

    const float2 textureSize = float2(triangleColors.get_width(), triangleColors.get_height());
    const int2 z1 = trixelOriginOffsetZ1(int2(textureSize));
    // Convert to pixel-space and apply the same parity-based row shift the
    // GLSL fragment uses (see `f_trixel_to_framebuffer.glsl`). Each iso
    // quad cell is split diagonally into two trixels; this picks which
    // row of the trixel canvas this fragment maps to.
    float2 origin = in.texCoords * textureSize;
    const int originModifier = trixelOriginModifier(z1, frameData.canvasOffset);
    origin = trixelFramebufferSamplePosition(origin, originModifier);

    const float2 sampleUv = origin / textureSize;
    float4 color = triangleColors.sample(triangleSampler, sampleUv);
    const uint2 readCoord = uint2(clamp(origin, float2(0.0f), textureSize - float2(1.0f)));
    const int rawDist = triangleDistances.read(readCoord).r;
    float depth = normalizeDistance(rawDist + frameData.distanceOffset, globals);

    const int subdivisions = max(int(frameData.effectiveSubdivisionsForHover.x), 1);
    const float2 hoveredPosition =
        frameData.mouseHoveredTriangleIndex * float(subdivisions) +
        float2(z1) +
        frameData.canvasOffset;
    const int2 originIndex = int2(floor(origin));
    const int2 hoveredIndex = int2(floor(hoveredPosition));
    const bool isMouseHovered = all(hoveredIndex == originIndex);
    if (isMouseHovered) {
        if (color.a >= 0.1f && depth <= hovered.hoveredDepth) {
            const uint2 entityId = triangleEntityIds.read(readCoord).rg;
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

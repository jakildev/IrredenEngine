#include <metal_stdlib>
using namespace metal;

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
    constant GlobalConstants& globals [[buffer(1)]]
) {
    FragmentOut out;
    constexpr sampler triangleSampler(coord::normalized, address::clamp_to_edge, filter::nearest);
    const float4 color = triangleColors.sample(triangleSampler, in.texCoords);
    if (color.a < 0.001) {
        discard_fragment();
    }

    const float2 textureSize = float2(triangleColors.get_width(), triangleColors.get_height());
    const float2 clampedTexCoords = clamp(
        in.texCoords,
        float2(0.0f),
        float2(0.999999f)
    );
    const uint2 distanceCoord = uint2(clampedTexCoords * textureSize);

    out.color = color;
    out.depth = normalizeDistance(triangleDistances.read(distanceCoord).r, globals);
    return out;
}

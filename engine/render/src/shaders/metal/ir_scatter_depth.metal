#include "ir_iso_common.metal"

// Encode the face/cell tie code on the D24 lattice: naive 2^-24 normalized
// offsets can collapse at depth 0.5. The upper-half correction avoids that
// rounding tie using exactly representable power-of-two operations.
inline float scatterFinalDepth(float depth, float cellTieOffset) {
    const float halfStep = kScatterCellTieStep * 0.5;
    const float code = floor(depth / kScatterCellTieBand) * (kScatterCellTieBand / halfStep) +
        cellTieOffset / halfStep;
    return (code + (code >= 0.5 / halfStep ? 1.0 : 0.0)) * halfStep;
}

// The depth a fragment competes with. depthSlope is the face's depth change
// per unit of each in-plane parameter; a fragment on an edge across which the
// face recedes toward its interior yields one tie band.
inline float scatterEdgeTieDepth(float depth, float2 depthSlope, float2 quadParam) {
    const float2 inwardSlope = mix(depthSlope, -depthSlope, step(0.5f, quadParam));
    const float2 edgeDistance = min(quadParam, float2(1.0f) - quadParam);
    const bool2 recedingEdge = bool2(
        edgeDistance.x < kScatterEdgeTieWidth && inwardSlope.x > 0.0f,
        edgeDistance.y < kScatterEdgeTieWidth && inwardSlope.y > 0.0f
    );
    return any(recedingEdge) ? depth + kScatterCellTieBand : depth;
}

// The depth a face rasterizes at, the same on all four corners: no greater than
// the depth any of its fragments writes, so the fragment output can declare
// depth(greater) and the rasterizer may reject an occluded fragment before it
// is shaded. The face's nearest corner bounds every interpolated depth,
// scatterFinalDepth floors by under one tie band, and every other term adds
// depth. depthSlope and quadParam are the corner's own, as scatterEdgeTieDepth
// takes them.
inline float scatterEarlyDepth(float cornerDepth, float2 depthSlope, float2 quadParam) {
    const float nearestCorner = cornerDepth - dot(depthSlope, quadParam) +
        min(depthSlope.x, 0.0f) + min(depthSlope.y, 0.0f);
    return clamp(nearestCorner - kScatterEarlyDepthMargin, 0.0f, 1.0f);
}

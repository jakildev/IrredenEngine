#include "ir_iso_common.glsl"

// Encode the face/cell tie code on the D24 lattice: naive 2^-24 normalized
// offsets can collapse at depth 0.5. The upper-half correction avoids that
// rounding tie using exactly representable power-of-two operations.
float scatterFinalDepth(float depth, float cellTieOffset) {
    const float halfStep = kScatterCellTieStep * 0.5;
    const float code = floor(depth / kScatterCellTieBand) * (kScatterCellTieBand / halfStep) +
        cellTieOffset / halfStep;
    return (code + (code >= 0.5 / halfStep ? 1.0 : 0.0)) * halfStep;
}

// The depth a fragment competes with. depthSlope is the face's depth change
// per unit of each in-plane parameter; a fragment on an edge across which the
// face recedes toward its interior yields one tie band.
float scatterEdgeTieDepth(float depth, vec2 depthSlope, vec2 quadParam) {
    const vec2 inwardSlope = mix(depthSlope, -depthSlope, step(0.5, quadParam));
    const vec2 edgeDistance = min(quadParam, vec2(1.0) - quadParam);
    const bvec2 recedingEdge = bvec2(
        edgeDistance.x < kScatterEdgeTieWidth && inwardSlope.x > 0.0,
        edgeDistance.y < kScatterEdgeTieWidth && inwardSlope.y > 0.0
    );
    return any(recedingEdge) ? depth + kScatterCellTieBand : depth;
}

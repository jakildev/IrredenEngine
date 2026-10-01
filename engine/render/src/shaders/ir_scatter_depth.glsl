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

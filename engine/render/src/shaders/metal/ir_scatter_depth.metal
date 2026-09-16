#include "ir_iso_common.metal"

// The low D24 bit prefers exact coverage over a margin with the same face/cell
// code. Encode on the integer D24 lattice: naive 2^-24 normalized offsets can
// collapse at depth 0.5. The upper-half correction avoids that rounding tie
// using exactly representable power-of-two operations on both depth formats.
inline float scatterFinalDepth(float depth, float cellTieOffset, bool inMargin) {
    const float halfStep = kScatterCellTieStep * 0.5;
    const float code = floor(depth / kScatterCellTieBand) * (kScatterCellTieBand / halfStep) +
        cellTieOffset / halfStep + (inMargin ? 1.0 : 0.0);
    return (code + (code >= 0.5 / halfStep ? 1.0 : 0.0)) * halfStep;
}

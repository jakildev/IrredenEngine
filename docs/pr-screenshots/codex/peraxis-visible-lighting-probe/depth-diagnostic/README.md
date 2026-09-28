# Cross-pass depth diagnostic

For every wrongly rejected frame pixel, replay temporarily returned the positive
difference between its depth code and the prepass minimum, encoded low byte in R,
next byte in G, next byte in B, retaining its actual final depth. All 96 missing
pixels returned code 32: one `scatterFinalDepth` quantization band. The original
coverage/geometry was not changed. See `delta.json` and full diagnostic captures.

This identifies a native cross-pass depth disagreement, consistent with separate
fragment compilation/interpolation. It does not establish precisely which compiler
operation differs. The subsequent experiment uses one runtime-selected fragment
program for both phases; it does not widen the rejection threshold.

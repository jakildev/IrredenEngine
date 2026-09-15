# One overflow record per voxel face

The voxel stage's 2×3 local layout has two lanes per face. Cardinal rendering
needs both triangles, but per-axis overflow stores a complete face record. Both
lanes previously appended the same position, depth and color. Canonical sorting
orders these records; it does not deduplicate them. The scatter therefore drew
the same face twice and the duplicate consumed overflow capacity.

The GLSL and Metal stage-1 overflow branches now select the existing
`faceOffset_2x3(slot, 0)` lane before appending. Other store, winner-election,
stage-2, cardinal and detached-triangle paths keep their execution structure.
The resulting face has the same geometry, depth and lighting inputs; this adds
no smoothing, blur or coverage inflation.

## Measurement

Native Metal, M4 Max, Debug, animated IRPerfGrid 64³, yaw 45°, zoom 4,
FULL/base 1. Each phase uses three fresh 300-frame runs with source state and
binary/shader fingerprints. Original measurements are in
[the frozen-control audit](rotation-controls/early-axis-baseline/); a restored
original and both single-writer candidates are in
[the evidence directory](per-axis-single-face-writer/).

The broad exploratory candidate skipped a lane for every per-axis operation.
It reduced scatter from 2.997 to 2.486 ms; restoring the original returned it
to 2.999 ms. Frame means were 22.470 → 21.397 → 22.003 ms. However, one frozen
entity-rotation pose changed 12 face-junction pixels reproducibly. The final
patch is restricted to overflow appends; the broad candidate is not shipped.

The final overflow-only version measures **2.474 ms scatter** (2.471–2.476),
versus 2.999 ms (2.989–3.005) in the restored original: a 17.5% reduction.
Frame mean is **21.310 ms** (20.910–21.610), versus 22.003 ms
(21.620–22.310), about 3.2% lower. Storage is 5.060 versus 5.085 ms,
finalization 2.905 versus 2.903 ms, and finite shadow faces 0.672 versus
0.671 ms. The result is downstream draw work saved; it does not establish
a general speedup in the unchanged stages. Timings remain sampled GPU
invocations on this host, not hardware counters or multi-canvas frame totals.

## Correctness checks

The final candidate is compared against the unchanged parent renderer using
native captures, with per-frame overlay pixels excluded only in IRPerfGrid.
All 57 final pairs match exactly: 15 IRPerfGrid scene regions (x < 1800,
full height), 24 complete fog frames and 18 complete CanvasStress frames.
[Representative full screenshots and all pair hashes](../pr-screenshots/codex/per-axis-single-face-writer/)
are retained alongside the PR. Native builds of all three demos, changed-line
formatting, header/Metal registry checks and comment-reference lint pass.

The critical extra control holds entity rotation at 0.47 radians while camera
yaw traverses all quadrants. All nine full CanvasStress frames match the
original after narrowing the patch; the problematic 12 pixels match too.
Coverage includes attached entities and detached/revoxelized canvases in the
default scene. The 24-pose fog cross-section fixture exercises face selection
at partially revealed boundaries rather than only checking unoccluded cubes.

Saturated overflow lists can produce different images because fewer duplicate
appends leave room for real faces previously dropped at capacity. That recovery
is expected, but no saturated-scene equivalence or OpenGL runtime result is
claimed here. Unrelated equal-depth winner scheduling remains a follow-up;
the narrow change avoids altering those operations.

## Remaining performance work

Expose generated samples, occupied cells, live overflow entries and scratch
bytes with producer-matched readback. Separate screen-visible work from finite
shadow-caster work before tightening rotated culling. Measure overflow sort
launches versus live work, then light-volume and CPU update/upload costs.
The current evidence establishes a downstream scatter improvement, not rotation
parity or a reduction in every per-axis stage.

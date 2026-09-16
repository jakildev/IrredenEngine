# One overflow record per voxel face

The voxel stage's 2×3 local layout has two lanes per face. Cardinal rendering
needs both triangles, but per-axis overflow stores a complete face record. Both
lanes append the same position, depth and color. Canonical sorting
orders these records; it does not deduplicate them. The scatter therefore draws
the same face twice and the duplicate consumes overflow capacity.

**Historical rejected experiment; the [validated retry](overflow-face-dedup.md)
now ships the narrow overflow-only guard.** A fresh stack-integration control found a
12-pixel lighting change with the overflow-only lane guard enabled. At that checkpoint both shader
branches retained the parent renderer's two-lane append behavior. The measurements
below describe rejected candidates, not measurements of the validated retry.

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
entity-rotation pose changed 12 face-junction pixels reproducibly. The narrower
candidate is restricted to overflow appends; neither candidate is shipped.

The rejected overflow-only version measures **2.474 ms scatter** (2.471–2.476),
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
All 57 pairs in that original run matched exactly: 15 IRPerfGrid scene regions (x < 1800,
full height), 24 complete fog frames and 18 complete CanvasStress frames.
[Representative full screenshots and all pair hashes](../pr-screenshots/codex/per-axis-single-face-writer/)
are retained alongside the PR. Native builds of all three demos, changed-line
formatting, header/Metal registry checks and comment-reference lint pass.

The critical extra control holds entity rotation at 0.47 radians while camera
yaw traverses all quadrants. All nine full CanvasStress frames matched the
original in that run after narrowing the patch. The subsequent control below
shows that this result was insufficient to establish equivalence.
Coverage includes attached entities and detached/revoxelized canvases in the
default scene. The 24-pose fog cross-section fixture exercises face selection
at partially revealed boundaries rather than only checking unoccluded cubes.

Saturated overflow lists can produce different images because fewer duplicate
appends leave room for real faces previously dropped at capacity. That recovery
is expected, but no saturated-scene equivalence or OpenGL runtime result is
claimed here. Unrelated equal-depth winner scheduling remains a follow-up;
the narrow change can still affect the order of tied overflow draws.

## Stack-integration control

At integrated head `910fa3f0ee7c71600a89b92577b90a9e3035af0a`, run:

```text
fleet-run IRCanvasStress --no-auto-rotate --no-spin --frozen-pose 0.47 --sweep-yaw 0 6.2831853 9 --auto-screenshot 6
```

Two fresh narrow-candidate sweeps (896–904 and 906–914) each match eight of nine
original full RGB frames. At yaw 45°, twelve pixels in `[1300,1306) × [588,590)`
change from `(86,184,86)` to `(76,161,76)` at a green face junction. Removing
only the Metal overflow lane guard, rebuilding the shader assets and rerunning
the same binary/command (915–923) restores the original pixels; all nine frames
match 878–886. This is an output-equivalence acceptance failure despite unchanged face
geometry. A single-pose invocation does not reproduce the sweep's darker seam.

[Candidate and restored-control screenshots](../pr-screenshots/codex/per-axis-single-face-writer/stack-review-control/)
retain the evidence. Sorting is conditional on displaced-cell tie detection and
a lagged nonempty count; equal-depth cross-cell overflow ordering is a candidate
cause, not a proven diagnosis. Resolve and test that ordering before reviving
single-writer appends. Do not adjust depth tolerance or blur the seam.

## Remaining performance work

Expose generated samples, occupied cells, live overflow entries and scratch
bytes with producer-matched readback. Separate screen-visible work from finite
shadow-caster work before tightening rotated culling. Measure overflow sort
launches versus live work, then light-volume and CPU update/upload costs.
The rejected candidate demonstrates potential downstream savings, not a shipped
scatter improvement or rotation parity.

## Subsequent frozen-scene diagnosis

[Coverage arbitration](../design/frozen-scatter-flicker.md) reproduces the same
12-pixel flicker with overflow drawing disabled and fixes the exact/margin tie
in the regular cell draw. The earlier rejection remains an honest failed
control, but its attribution to overflow is unproven. Retry deduplication with
repeated frozen captures and a stable parent before claiming equivalence.

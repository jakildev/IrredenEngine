# Parallel analytical-box face indexing

The analytical-box caster assigns its three light-facing source faces to lanes
0–2 of the first Z workgroup. Each lane uses the existing finite-face indexer;
face geometry, record capacity, tile capacity, receiver queries and fallback
rasterization are unchanged. No new dispatch, buffer or synchronization is added.
The record and tile-slot allocations already use atomics. The existing
post-dispatch storage barrier publishes the completed index to consumers.

## Native measurement

Apple M4 Max, macOS 26.5.2, Metal Debug, AC power. IRCanvasStress, fixed camera
yaw 0, zoom 1, subdivision 1, origin pivot, no animation. Framebuffer captures are 2560×1440. A thin 120-unit SDF
floor and analytical caster are the only selected groups; ordinary lighting,
AO and shadows remain enabled. The span control changes caster X/Y extent,
retaining Z extent 8. The count control creates coincident identical casters:
it measures index/raster pressure, not a representative game layout.

`python3 scripts/perf/box_shadow_controls.py --rounds 2 --output <fresh-directory>`
runs spans 18, 128 and 512 with one caster, plus span 18 with 65 casters.
Each matrix reverses case order in its second round. We ran original,
candidate, original again, candidate again, for four observations per arm.
All 32 runs use one unchanged demo binary and runtime-script set. The final
candidate shader differs from the first candidate only by redundant-brace
removal and indentation. Final formatting was then rebuilt and smoke captured.

| Case | Original frame ms (range) | Candidate frame ms (range) | Original / candidate GPU envelope ms |
|---|---:|---:|---:|
| 18, one | 9.352 (9.33–9.39) | 9.430 (9.34–9.53) | 5.969 / 5.554 |
| 128, one | 9.370 (9.34–9.40) | 9.418 (9.36–9.54) | 6.212 / 6.099 |
| 512, one | 21.082 (21.04–21.11) | 16.953 (16.90–16.99) | 20.163 / 16.046 |
| 18, 65 | 9.373 (9.35–9.38) | 9.500 (9.35–9.64) | 6.181 / 5.815 |

The large-box wall mean improves about 19.6%; its GPU envelope improves about
20.4%. `shapeCastBoxes` falls from 14.952 to 10.927 ms. This timer encloses
indexing plus fallback rasterization, not the indexing body alone. Smaller
cases show no wall-time win and a small increase within the observed candidate
spread; no general or million-entity improvement is claimed.

These are capture-assisted runs (275 frames, two screenshots per run), so
wall-time tails include image capture. Means include startup; GPU envelopes
include stalls and are not GPU busy time. Stage means are not additive frame
budgets. Reports retain steady wall statistics separately. This is neither a
Release throughput qualification nor an OpenGL timing result.

[Raw reports, manifests and tables](box-shadow-index-lanes/) preserve each
arm. Original-repeat used only the two original staged box shaders; final
staging is restored to the source tree. A one-box control contains 176 total
entities; the 65-box control contains 240. This independently confirms the
count arm instantiated its extra 64 entities.

## Correctness

All 64 timing captures match their corresponding original fixture pixels;
that includes the original set itself and 48 subsequent comparisons. The
large plate fills most of the viewport and is primarily a scheduling stress
case. The small caster shows the projected shadow on the floor.

The rebuilt final demo also passed a nine-pose full-turn mixed-mode sweep
(0° through 320° in 40° steps): all nine before/after RGB images are identical.
[Captures and reproduction command](../pr-screenshots/codex/box-shadow-index-controls/README.md).
This establishes appearance preservation in those fixtures, not absence of
pre-existing visual defects.

The production GLSL and Metal emission adapters check 952,576 rays each
against an independent oriented-box slab intersection. They simulate all 64
lanes in 32 Z workgroups and reject a missing face lane, repeated groups,
wrong polarity, unrotated edges, half-length edges and lost translation.
The shared indexer capacity and guard-word tests also pass.

At global face-record exhaustion, atomic allocation order can change which
faces get exact records and which tiles fall back. Coverage remains protected
by the existing incomplete-tile marker and sampled fallback, but byte identity
is not promised for that regime. This change does not fix its jagged edges.

Open PR #3804 edits the same box-emission body to align caster/receiver centers.
Its geometry correction is independent: preserve its rendered-center expression
when reconciling, and preserve this lane ownership. This work does not claim to
fix or validate that unmerged visual correction.

## Remaining work

- Distribute tiles of a single large face across lanes if measurement justifies
  the extra publication/synchronization complexity. Three-face concurrency
  leaves each individual face tile loop serial.
- Measure tile-candidate counts and incomplete-tile/global exhaustion separately
  before changing index capacity or conservative face-versus-tile rejection.
- Retain exact geometry for projected edges; no blur, footprint inflation or
  receiver bias is part of this optimization.
- Native OpenGL smoke and performance comparison remain due.

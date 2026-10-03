# Voxel workgroup occupancy

Adopt 32 Z lanes with several whole low-density voxels sharing a group. This
avoids the idle-lane cost of the earlier [global-32 experiment](../voxel-dispatch-packing/README.md).
Every requested face-half sample remains present exactly once; subdivision,
geometry, shadows and depth arbitration are unchanged. The shared writer/consumer
contract and its bijection proof are in [the design](../../design/voxel-dispatch-occupancy.md).

## Native controls

Metal, macOS, Debug, AC power, 262,144 frozen-wave voxel entities, FULL subdivision,
origin pivot, no overlay, stage profiling enabled. Each table entry averages two
120-frame runs, excluding the first 30 frames of each run from frame statistics.
GPU scope means include their report's full sampling interval. Commands, actual
camera witnesses, hashes, host load and clean exits are in the adjacent manifests.
Reports/logs are retained with trailing whitespace stripped.

Order was baseline A, candidate B, rebuilt baseline A2, rebuilt candidate B2.
A covers dense/low/rotated; the other batches also cover subdivision 3 and 5.
Each batch runs cases in table order. A2 and B2 each hold one shared fleet benchmark
reservation across their ten runs; builds occur outside that reservation.
These are grouped return controls, not randomized per-run interleaving.

| Case | A steady ms | B steady ms | A2 steady ms | B2 steady ms |
|---|---:|---:|---:|---:|
| Cardinal zoom 4/base 4, effective 16 | 98.845 | 89.505 | 100.145 | 83.755 |
| Cardinal zoom 1/base 1 | 10.815 | 10.460 | 10.145 | 9.285 |
| Yaw 45°, zoom 4/base 4, capped 8 | 21.655 | 17.050 | 22.370 | 16.875 |
| Cardinal zoom 1/base 3 | — | 13.590 | 11.595 | 10.620 |
| Cardinal zoom 1/base 5 | — | 18.130 | 19.975 | 17.910 |

Dense and rotated gains persist when returning to baseline. The subdivision-3
candidate initially has worse frame timing than A2 despite a lower GPU envelope;
B2 does not repeat that regression. Low-density frame ranges overlap. Keep these
observations rather than claiming every density reliably improves frame time.
The GPU envelope is consistently lower in these controls:

| Case | A GPU envelope ms | B ms | A2 ms | B2 ms |
|---|---:|---:|---:|---:|
| Dense | 89.090 | 76.167 | 89.852 | 75.175 |
| Low | 7.748 | 6.980 | 7.341 | 6.364 |
| Rotated | 16.669 | 12.428 | 17.851 | 13.622 |
| Subdivision 3 | — | 7.947 | 8.665 | 7.875 |
| Subdivision 5 | — | 14.116 | 15.988 | 13.881 |

In A2→B2, dense depth/election/color scope means change from
29.168/28.117/26.909 ms to 24.012/22.992/22.670 ms. Low-density depth changes
2.373→0.362 ms, but the whole frame improves far less. Subdivision-3 election
increases 1.464→1.562 ms while depth decreases 2.912→1.674 ms. The scheduling
change does not make every pass cheaper. Light-volume intervals also grow as
geometry shortens. GPU scopes include stalls/overlap and are not additive busy
time; attributing those shifts requires deeper native profiling.

All rotated runs retain a peak of 745,398 overflow entries and zero drops.
The host is shared with fleet work, and frame scheduling varies between batches.
These controls support this bounded occupancy change; they do not establish
native OpenGL performance, Release throughput, rotation parity at equal effective
density, or one million entities at 60 FPS.

## Correctness and provenance

- The executable GLSL/Metal test runs the actual compact finalizer, dispatch
  writer, shared helpers and stage consumer prefixes. Distinct list counts,
  empty/partial groups, XY spill, densities 0–16, all modes, axis routes and
  feeder routing are covered. Sixteen mutations reject missing guards/strides
  and incorrect list/domain selection; layout checks cover all five variants.
- All 64 render harness suites pass; header checks pass for 630 headers,
  40 Metal kernels and 98 GLSL files. Native IRPerfGrid and IRCanvasStress build.
- CanvasStress passes all 24 checks: 14 exact RGB matches and 10 existing
  structural checks, without changing references or thresholds. Three PerfGrid
  views at yaw 0°/45°/90° are RGB-identical to the complete-coverage references.
  [Screenshots and limits](../../pr-screenshots/codex/voxel-workgroup-occupancy/README.md).

Measurements use base `171778ee09660c2b771eeb25571cfa16fb96dd3b`. Baseline
return runs reverse only the candidate production edits; staged helper files
are also removed from the shader asset directory before rebuilding. The A/A2
shader and runtime-script hashes match; rebuilt Debug binary hashes differ.
The candidate production diff is [retained](measured-source.patch), with its two
new [GLSL](measured-ir_voxel_dispatch.glsl.txt) and
[Metal](measured-ir_voxel_dispatch.metal.txt) helpers. Tests/docs remain dirty
during runs, as the manifests report. Capture provenance has its own patch;
a subsequent registry-comment cleanup has no rendering effect.

Next: conservative overflow cost, continuous camera/density timing, feeder-heavy
populations, and native OpenGL validation. The low-density control retains all
262,144 candidates. Measure shared lighting work, submission/readback stalls and
conservative occlusion before further dispatch tuning; culling must preserve
finite-face, fog and off-screen shadow coverage.

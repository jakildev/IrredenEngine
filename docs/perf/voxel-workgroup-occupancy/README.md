# Voxel workgroup occupancy

Pack several whole low-density voxels into each Metal group with 32 Z lanes.
OpenGL keeps one voxel per XY group with eight Z lanes; its packing experiments
regressed in the Linux diagnostics below. This
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
  feeder routing are covered. Fifteen mutations reject missing guards/strides
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

## Latest-master integration

Integrated master `b3e3d1c35d3eb6cad44da12777510400b6a37f00` after measuring.
Its viewport writer still referenced the count-based resample cache removed by
the shared-canvas change, so it did not compile. The final stack consumes the
existing viewport repair in PR #4105 unchanged: rotated writes notify the pool
content generation. Viewport source and tests match that parent exactly.
The viewport still rewrites unchanged subjects every frame; no-op rewrite and
identity/rotation transition coverage remain separate follow-ups.

IRCanvasStress, IRPerfGrid and IrredenEngineTest build on the integrated tree.
All 65 render-tooling suites pass, and header checks pass for 646 headers,
40 Metal kernels and 98 GLSL files. The expanded native CanvasStress suite passes
all 30 checks: 20 exact RGB comparisons, including shared parts and detach, and
10 structural checks. [Complete integration log](../../pr-screenshots/codex/voxel-workgroup-occupancy/integration-render-verify.log).
The matched timing controls above remain tied to their earlier measured base;
they were not rerun against the viewport/LOD integration.

The final shared stack base also passes all 39 viewport/Lua binding tests.

## Linux same-runner diagnostic

The initial historical CI gate reported a 13.6% zoom-4 regression. A
[head/base/head diagnostic](https://github.com/jakildev/IrredenEngine/actions/runs/37176429906)
isolates it on one hosted Linux runner. Both revisions build the identical
OpenGL binary; only the runtime shader fingerprints differ. The first and
return head shader hashes match. All matrices use 262,144 entities, FULL mode,
base subdivision one, 60 frames per cell, with 15 warm-up frames excluded from
the steady figures below. Full-frame measurements, stage timings, calibration,
logs and hashes are retained in [linux-wide-pair](linux-wide-pair/paired/provenance.json).
Whitespace is stripped from retained text. The shared baseline writer was skipped.

| Cell | Base steady ms | First 32-lane head | Return 32-lane head |
|---|---:|---:|---:|
| Zoom 1 | 795.39 | 759.60 (-4.5%) | 751.06 (-5.6%) |
| Zoom 4, effective subdivision 4 | 1985.24 | 2330.65 (+17.4%) | 2324.68 (+17.1%) |

At zoom 4 the 32-lane path schedules the same number of invocations as the
eight-lane parent. Stage-2 scope cost rises from 349.20 to 529.73/520.31 ms.
The regression repeats, so this is not accepted as historical-baseline noise.
OpenGL returns to eight-slice physical groups with the shared packing math
retained; Metal remains at 32. The eight-lane paired measurement
below tests whether that physical-width change is sufficient. This Linux result does not
qualify hardware OpenGL performance or native OpenGL visual presentation.

Master `52eca06ad` is integrated after that diagnostic. The viewport repair
and fleet fixture registry fix are merged; the latter's subject check passes.
IRCanvasStress, IRPerfGrid and IrredenEngineTest rebuild successfully.

### Eight-lane OpenGL control

The [second same-runner diagnostic](https://github.com/jakildev/IrredenEngine/actions/runs/37177793046)
compares `8c4a5d499` with integrated master `52eca06ad`. Full reports and
provenance are retained in [linux-small-pair](linux-small-pair/paired/provenance.json).

| Cell | Base steady ms | First eight-lane head | Return eight-lane head |
|---|---:|---:|---:|
| Zoom 1 | 787.85 | 751.65 (-4.6%) | 764.95 (-2.9%) |
| Zoom 4, effective subdivision 4 | 2005.48 | 2318.31 (+15.6%) | 2319.32 (+15.6%) |

Physical width alone does not remove the regression. The next candidate uses
the exact dense-domain identity in the shared lane helper: when `S >= P`,
`V = 1`, `q = 0`, and `r = l`. Returning that index directly avoids the
variable quotient/remainder work. The third diagnostic below measures this
candidate; the regression remains open until the final policy is validated. Coverage tests include `P-1`, `P`, and
`P+1` as well as non-divisor sample counts and the supported render domains.

Latest-master native validation passes all 30 CanvasStress checks and 40
viewport/LOD/cursor tests. The [log](../../pr-screenshots/codex/voxel-workgroup-occupancy/latest-master-render-verify.log)
and [provenance](../../pr-screenshots/codex/voxel-workgroup-occupancy/latest-master-provenance.json)
identify the pre-direct-index Metal assets actually tested.

### Direct-index control and backend scope

The [third same-runner diagnostic](https://github.com/jakildev/IrredenEngine/actions/runs/37178863947)
compares direct-index candidate `c32b40aa7` with master `52eca06ad`.
[Raw reports and provenance](linux-direct-pair/paired/provenance.json) retain
both head runs.

| Cell | Base steady ms | First direct-index head | Return direct-index head |
|---|---:|---:|---:|
| Zoom 1 | 787.72 | 742.53 (-5.7%) | 737.85 (-6.3%) |
| Zoom 4, effective subdivision 4 | 1975.26 | 2200.99 (+11.4%) | 2200.97 (+11.4%) |

The dense identity helps but does not make low-density packing acceptable on
this OpenGL backend. The final candidate limits cross-voxel packing to Metal.
OpenGL keeps the shared sample-domain/helper interface but uses its original
one-voxel XY ownership and direct slice indexing at every density. The writer
therefore folds to the original dispatch dimensions. The final paired check
below measures this policy; no OpenGL performance improvement is claimed.

Metal retains the originally measured packing helper without the dense-index
early return; that experiment is not part of the final backend policy.
The [native before/after experiment](metal-direct-experiment/) spanned an
overnight interruption between batches; low/rotated ranges overlap, and it
does not establish a Metal gain from that branch. Manifests record the actual
run times, compiled asset hashes and checkout state.

Master `1afd24020` is integrated after the final-policy diagnostic was launched.
It adds whole-body SDF fog, fog overrides/channel masks and host quiet-window
tooling. The voxel dispatch
writer, consumers and helpers are unchanged by the merge. Fresh native
integration validation is recorded separately from the pinned Linux pair.

The integrated tree builds IRPerfGrid, IRCanvasStress and IrredenEngineTest.
All 305 render-tooling tests pass; native viewport/LOD/cursor/fog coverage passes
183 tests with one OpenGL-only skip. Header checks cover 648 headers, 40 Metal
kernels and 98 GLSL files. The final native [CanvasStress log](../../pr-screenshots/codex/voxel-workgroup-occupancy/final-policy-render-verify.log)
passes all 30 checks (20 exact RGB and 10 structural), without changing
references or thresholds. A separate default capture exits cleanly and all six
reference views remain RGB-identical; [provenance](../../pr-screenshots/codex/voxel-workgroup-occupancy/final-policy-provenance.json)
records the integrated build and compiled asset fingerprints. Existing PR
images are retained because their RGB data is unchanged.

### Final OpenGL policy result

The [final same-runner diagnostic](https://github.com/jakildev/IrredenEngine/actions/runs/37217015393)
compares `4f472b6fe` with `52eca06ad`. The [raw archive](linux-final-pair/paired/provenance.json)
retains every matrix, log, fingerprint and comparison. Absolute times differ
from earlier hosted machines; compare only each run against its own base.

| Cell | Base steady ms | First final-policy head | Return final-policy head |
|---|---:|---:|---:|
| Zoom 1 | 494.92 | 468.79 (-5.3%) | 495.36 (+0.1%) |
| Zoom 4, effective subdivision 4 | 1140.36 | 1174.48 (+3.0%) | 1190.26 (+4.4%) |

Full-frame zoom-4 means rise 1.8% and 3.2%, below the 10% frame-regression
threshold. The large packing regression is removed, but these controls do not
establish exact timing parity: steady zoom-4 remains 3–4% slower. Depth/color
scope means are 233.05/200.80 ms on base, 238.52/200.25 on first head and
244.43/204.34 on return head. Election rises from 7.12 to 10.46/10.84 ms;
its relative stage regression remains visible in the report. The first
zoom-1 full-frame p99 is worse (startup-inclusive measurement), while the
return improves.
No threshold or shared baseline was changed to accept the candidate.

The subsequent master merge changes no dispatch writer/consumer/helper code.
Native OpenGL presentation, hardware throughput and quiet Release/million
qualification remain separate TODOs.

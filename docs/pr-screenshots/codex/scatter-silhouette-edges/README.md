# Finite voxel faces under continuous camera yaw

Base: PR #3941, `02ad32f2cacfb1b9144093ad16b9c4abbcde30aa`.
Native captures: Apple M4 Max, macOS Metal Debug, 1280×720 game framebuffer,
2560×1440 nearest-neighbor output. Manifests record commands, binary and staged
shader hashes; logs retain clean exits and the settled `CaptureCamera` state.
`captured-shaders.patch` reproduces the seven changed shader files used by the
retained sweep/full-scene captures; their hashes were checked against the manifests.
Final cleanup removes their unused mask writes, dead margin helpers and stale
comments without changing finite-face emission or coverage. The performance runs
already omit mask writes, and the final native regression rerun checks the cleanup.

Requested zoom 0.5 clamps to actual zoom 1 in this demo; the third sweep exercises
base subdivision 8, not a genuine half-size camera zoom.

## Geometry and visibility

GRID scatter draws the finite voxel face quad directly. Hardware triangle
coverage owns the silhouette; fragment interpolation supplies planar depth and
finite-face lighting/shadow positions. There is no dilation, blur, alpha edge
filter, neighbor-alpha seam inference or margin depth correction on this path.
Source/detached coverage paths remain unchanged; unused GRID margin helpers
and their obsolete constants are removed.

Two defects interacted. Conservative margins could extend a face over a different
surface and win its depth test. Meanwhile the overflow lane discarded genuinely
visible finite faces by comparing depth only around the face origin. An origin
sample cannot certify occlusion over a quad. Margins sometimes hid the omitted
face by painting the wrong neighboring normal into the hole. Exact faces expose
that missing emission, so both corrections are required.

The overflow lane retains cardinal-store losers and resolves visibility through
fragment depth. Capacity already covers at most three emitted records per voxel.
The old mask scratch allocation is reserved for layout compatibility; no mask
read or write is needed for this decision. See the independent
[overflow counterexample](overflow-mask-analysis.md) and executable reproduction.

## Independent expectations

`render-orbit-geometry-metric.py` reconstructs the frozen orbit6 inverse-resampled
12³ cube independently of shader projection helpers. At yaw 135°, zoom 4 it checks
82,344 output pixels (20,586 game samples): parent has 436 excess silhouette
pixels; candidate has **zero extra, missing or wrong-face pixels**, with zero
boundary tolerance. One-pixel mutations prove every failure class is rejected.

The broader [17-yaw ray sweep](ray-sweeps.json) tests 909,689 game samples in a
geometry-derived projected bounding box. All 673 interior missing samples and
all 3 interior wrong-face samples disappear. Non-cardinal excess silhouette
samples fall to zero. The remaining non-cardinal discrepancies are 15 wrong-face
and 4 missing samples, all on a one-game-pixel geometric boundary band; requested
ideal angles versus settled float32 poses and raster edge ownership are not yet
fully isolated there. The five cardinal/end-point captures are RGB-identical to
the parent and retain their existing boundary differences from the ray oracle.

[Normal-facing checks](normal-facing-sweeps.json) cover 60 captures: 17 yaw poses
at each of three effective density settings plus 9 fractional pans. This is a
normal-direction gate, not a full-scene geometry proof. Full-scene captures include
the moving sphere pixel that rejected the intermediate seam-classification fix.

```sh
python3 docs/pr-screenshots/codex/scatter-silhouette-edges/ray-sweeps.py docs/pr-screenshots/codex/postmerge-shadow-correctness/orbit/fix-sweep docs/pr-screenshots/codex/scatter-silhouette-edges/sweeps/yaw-zoom4 /tmp/scatter-ray-results.json
python3 docs/pr-screenshots/codex/scatter-silhouette-edges/overflow-mask-analysis.py --output /tmp/scatter-mask-results.json
```

The comparison crops use nearest-neighbor magnification. Full captures and run
manifests remain under `sweeps/` and `full-scene/`; parent sweep captures are in
the preceding PR evidence directory named in the reproduction command.

## Rejected approaches

- Trimming all dilated margins analytically leaves precision/coverage disagreement
  between the expanded raster geometry and the secondary fragment test.
- Camera-facing seam classification alone still accepts unrelated storage neighbors.
- Depth-verified coplanar neighbors improve the silhouette but leave a moving
  sphere pixel missing; its two required records are present in GPU overflow.
- Sharing perpendicular seam fill with overflow fills that sphere pixel but adds
  wrong-face bands at other angles. Its patch is `rejected/shared-seams.patch`.
  A penetration-based margin slope does not bound the adjoining plane as a face
  approaches edge-on. Hardware finite faces avoid that extrapolation entirely.

## Pending work

1. Confirm OpenGL native captures on Windows; this host executes Metal, while
   host tests exercise both shader bodies.
2. Resolve remaining cardinal/boundary oracle differences using actual capture
   state and backend raster ownership rules, without widening acceptance limits.
3. Profile larger voxel populations before claiming scalability; conservative
   overflow emission trades additional complete geometry for removing unsafe
   occlusion. Any future rejection needs a finite-footprint coverage/depth proof.
4. Remove the reserved origin-mask scratch region in a separately validated buffer
   layout cleanup; keep source/detached coverage paths independently justified.

## Performance control

Three Debug runs per variant, frozen full CanvasStress scene at yaw 292.5°,
zoom 1/subdivision 1, 363 frames each (first 90 excluded from steady CPU/frame
statistics). Parent shader files were staged from the base commit in the same
binary; `perf/parent-staged-shaders.json` and per-run shader hashes identify that
control. Source files were never reverted. Final runs use the cleaned shaders.

| Measurement | Parent | Finite faces + complete overflow |
|---|---:|---:|
| GPU scatter mean | 0.080ms | 0.066ms |
| GPU scatter run range | 0.079–0.082ms | 0.064–0.068ms |
| GPU whole-frame envelope mean | 5.982ms | 5.894ms |
| GPU envelope run range | 5.860–6.134ms | 5.745–6.128ms |
| Steady frame mean | 10.413ms | 10.340ms |
| Maximum overflow entries | 740 | 767 |
| Dropped overflow records | 0 | 0 |

Whole-frame timing ranges overlap: **no demonstrated whole-frame speedup**.
The measured scatter stage is cheaper in this workload, while overflow grows
about 3.6%. This small static scene does not bound million-entity workloads or
rotation-time peaks. Raw reports, logs, environment and shader fingerprints are
under [perf/](perf/).

A second paired control uses IRPerfGrid voxel_set, 32³ entities, frozen wave,
yaw45°, zoom1, 180 frames and three runs per variant. Actual visible candidates
are 32,768 in all runs. Base 1 uses effective subdivision 1; requested base 4 is
capped by the rotating per-axis store to density 2 (recorded in the logs), so this
does **not** validate full density 4 rotation.

| GRID control | Parent | Finite faces + complete overflow |
|---|---:|---:|
| Base 1 GPU scatter | 1.010ms | 0.696ms |
| Requested base 4 GPU scatter | 0.971ms | 0.692ms |
| Base 1 GPU envelope | 6.369ms | 6.165ms |
| Requested base 4 GPU envelope | 6.529ms | 6.276ms |
| Maximum overflow entries (both densities) | 75,166 | 88,647 |
| Dropped records | 0 | 0 |

All six final runs keep complete overflow storage. The extra records increase
lighting/sort exposure, but eliminating expanded faces and seam work reduces the
measured scatter cost. Steady frame means remain about 8.4ms; these static Debug
controls do not prove a million entities at 60fps. `perf/grid-*` retains reports,
commands, observed camera poses, bounds and shader fingerprints.

## Final validation

`render-verify.py --target IRCanvasStress --no-build --timeout 120` passes all
14 checks on the final Metal build: eight RGB references match 100% exactly, and
six structural checks pass. Thresholds are unchanged. The
[eight-reference review](visual-review-stack-index.md) uses the exact PR3941
parent, not the older integrated stack captures. Final shader staging hashes are
in `final-source.json`; validation counts are in `final-validation.json`.

Native builds of IRCanvasStress and IRPerfGrid, header/Metal registry/GLSL keyword
checks, formatting and Ruff pass. All 61 script suites pass on the final code,
including the two-backend overflow and finite-surface mutation controls. Host shader tests execute both backends; native
OpenGL presentation is still pending. Per-run logs are compressed as `.log.gz`.

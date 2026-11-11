# Direct-mapped sun depth slabs: native evaluation (macOS / Metal)

Runtime: Claude, pool-5, 2026-11-11, Apple M4 Max / Metal, `macos-debug`.
Source: PR head `a484f2fd4` (rebased on master `480f39294`). The previous
Codex attempt's partial evidence (zero arm only; associative capture blocked
by the Codex sandbox's missing WindowServer access) is superseded by this
run and kept at its own SHA-pinned location.

**Verdict: direct mapping keeps every recovery but fails the locked budget.
Ship zero slots; the variant stays opt-in and compiled out.**

## Frozen arms

Four binary-plus-shader trees, each built from the same revision with only
the three layout twins changed (`set_arm.py`), copied out of the build dir
and hashed (`freeze_arm.py`); manifests and patches under `frozen/`.

| arm | slots | direct | IRCanvasStress sha256 (prefix) |
|---|---|---|---|
| zero | 0 | (n/a) | `c1bd8da21b21e1d9` |
| associative | 3 | false | `54b144bfb160f355` |
| direct | 3 | true | `da1f2121715b5d2c` |
| master (`480f39294`) | 0 | (n/a) | control for criterion 4 only |

## Criterion 2: native capture matrix

`ir-acquire gpu -- python3 scripts/perf/sun_depth_slab_capture.py --output <dir> --build-dir <arm> --label <arm>`
for each arm: 46 entries / 58 shots per arm, every one `RESULT=CLEAN`.
Raw JSON: `captures/captures-{zero,associative,direct}.json` (probe CSVs and
run logs gzipped beside each case: `primary.csv.gz`, `run.log.gz`). Cross-arm
grade: `grade.py captures` → `captures/cross-arm.json`.

Missing / excess shadow pixels outside the 2-pixel band, receiver crop versus
its lower-only control:

| case | zero | associative | direct | direct collisions (cascade 0/1) |
|---|---|---|---|---|
| stacked-8-h60 (also `-last`, `-128`, `-h90`) | 2347 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| layered-2-h60-step40 | 2347 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| oblique-stacked-8-h60 (also `-last`, `-128`, `-h90`) | 2428 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| oblique-layered-3/4-h60-step64 | 2428 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| oblique-stacked-8-h30 (in-window) | 296 / 772 | 0 / 772 | 0 / 772 | 0 / 0 |
| stacked-8-h60-revoxelize | 2773 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| stacked-8-h60-revoxelized | 2262 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| moving-8 static poses 1-6 | 2347-2608 / … | 0 / legit | 0 / legit | 0 / 0 |
| oblique-moving-8 static poses 0-6 | 468-2132 / … | 0 / legit | 0 / legit | 0 / 0 |
| **residual** layered-3/4-h60-step64 | 2347 / 0 | 2347 / 0 | 2347 / 0 | 437 / 379 |
| **residual** oblique-layered-2-h60-step40 | 2436 / 0 | 2344 / 0 | 2352 / 0 | 279 / 251 |

- Every fully repaired beyond-throw case is 0 missing / 0 excess in the
  direct arm, with zero direct collisions. The in-window oblique case keeps
  its 772 legitimate excess. The direct full frame is byte-identical to the
  associative full frame on 43 of the 44 static captures.
- Every moving shot equals its static pose in all three arms.
- Unchanged controls (`lower`, `floor`, `distant-8-h60`, `-unlit`, h30
  cardinal, revoxelize controls and both suns) are byte-identical to the
  zero arm in both slab arms.
- Residuals. The cardinal `layered-3/4` scenes hold more than three slabs on
  the probed texels: associative overflows (437/379 rows) and direct collides
  on the same texels; both answer nearest-only, identical to zero.
  `oblique-layered-2-h60-step40` is the scene the historical census flagged:
  the associative census has 2/2 complete rows whose slabs collide mod 3 (the
  four formerly non-overflow collisions) beside 277/249 overflowed rows.
  Direct mapping changes 44 receiver pixels relative to associative, and
  **all 44 equal the zero arm's pixel** — nearest-only fallback, never an
  unsupported shadow (excess stays 0).
- `receivers-zero-assoc-direct.png`: zero / associative / direct receiver
  crops for `stacked-8-h60`, `oblique-stacked-8-h60` and the residual
  `oblique-layered-2-h60-step40` (inspected, with the full frames).

## Criterion 3: locked timing gate

`python3 scripts/perf/sun_depth_slab_timing.py --output timing --baseline <zero> --head <direct> --arm associative=<associative> --pairs 5`,
all five cells, interleaved order reversing per pair. Each profile run took
its own guarded benchmark window through `ir-run`: **75 / 75 runs
`QUIET=guarded`, all CLEAN**. Full tables: `timing/ab-timing.md`; raw runs,
manifests and per-run JSON under `timing/<cell>/pair-<n>/<arm>/`.

| cell | row | base mean (range, sd) | direct | ratio | paired Δ (sd) | lower 95% | gate | verdict |
|---|---|---|---|---|---|---|---|---|
| canvas-lower | gpu:bakeSunShadowMap | 1.278 (1.047..1.362, 0.131) | 3.070 | 2.403x | +1.792 (0.380) | +1.430 | Δ ≤ 0.064 ms | **FAIL** |
| canvas-lower | steady_mean_ms | 9.354 | 12.420 | 1.328x | +3.066 | | ≤ 1.10x | **FAIL** |
| canvas-lower | steady_p99_ms | 18.092 | 21.504 | 1.189x | +3.412 | | ≤ 1.10x | **FAIL** |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.370 (1.261..1.420, 0.064) | 2.711 | 1.979x | +1.341 (0.353) | +1.005 | Δ ≤ 0.029 ms | **FAIL** |
| canvas-stacked-128 | steady_mean_ms | 10.980 | 14.746 | 1.343x | +3.766 | | ≤ 1.10x | **FAIL** |
| canvas-stacked-128 | steady_p99_ms | 19.994 | 24.784 | 1.240x | +4.790 | | ≤ 1.10x | **FAIL** |
| canvas-default-raster | steady_mean / p99 | 22.962 / 29.644 | 23.604 / 28.642 | 1.028x / 0.966x | | | ≤ 1.10x | pass |
| perf-grid-zoom1 | steady_mean / p99 | 23.830 / 34.120 | 23.490 / 33.586 | 0.986x / 0.984x | | | ≤ 1.10x | pass |
| perf-grid-zoom4 | steady_mean / p99 | 28.752 / 37.750 | 27.480 / 36.130 | 0.956x / 0.957x | | | ≤ 1.10x | pass |

Attribution (associative arm, same windows): bake +2.233 ms (2.747x) on
`canvas-lower`, +2.234 ms (2.630x) on `canvas-stacked-128`. Removing the
claim loads takes 0.44 / 0.89 ms off the associative cost, but the direct
writer still costs 28x / 46x the floor. Neither bake row passes by rule (i)
or (ii); no rule-(ii)-only pass exists. The frame rows fail too: the steady
frame-time bins show the slab arms pushing frames from the 8 ms vsync mode
into the 17-21 ms modes (`timing/ab-timing.md`, bin table).

`timing-unguarded-attribution/`: an earlier `canvas-lower` set run inside
one outer `ir-acquire benchmark` hold, where the nested runs report no
`QUIET` verdict. Not counted; it agrees in direction (direct bake +0.49 ms,
lower bound +0.18 ms, against 0.064).

## Criterion 4: regression and parity

- Host harness suites (all pass): `test_render_sun_depth_slab.py` (61,802
  direct write sets, 15,276 recoveries, 6,870 collisions, 48 splat-extrema
  residuals), `test_render_sun_depth_slab_tools.py`,
  `test_render_source_face_query.py`, `_overflow.py`, `_index.py`,
  `_index_coverage.py`, `test_render_shadow_layer_union.py`,
  `test_render_legacy_shadow_bake.py`, `test_render_mixed_source_shadow.py`,
  `test_render_projected_face_boundary.py`, `test_render_cooperative_face_index.py`,
  `test_render_grazing_face_index.py`, `test_render_impulse_shadow.py`,
  `test_render_detached_triangle_receiver.py`, `test_render_sun_hierarchy.py`,
  `test_sun_query_probe.py`, `test_harness_dependency_contract.py`;
  `shader_parity_audit.py`: 148 GLSL / 137 Metal, 5 allowlisted.
- `SunPrimaryDepthProbeRowTest.*`: 4/4 on the zero build and on the direct
  build.
- `render-verify.py --target IRPerfGrid` at zero slots: 21/21 pass.
- `light-verify.py --if-changed`: all checks pass.
- `render-verify.py --target IRCanvasStress`: 21/59 fail on the head. The
  same-session master control (`IRREDEN_BUILD_DIR=<master> render-verify.py
  --no-build --build-dir <master>`) fails the **identical 21 rows**
  (`validation/rv-canvas-{head,master}.labels`): inherited, tracked by the
  open macOS reference refresh, not caused by this change.
- Native OpenGL remains cross-host smoke debt (`fleet:needs-*-smoke`).

## Criterion 5: decision

Compiled out. All three layout twins ship `kSunDepthSlabSlots = 0` with
`kSunDepthSlabDirectMapped = true` as the opt-in selection; the capacity note
records the per-cell readings, the collision census and the residuals.

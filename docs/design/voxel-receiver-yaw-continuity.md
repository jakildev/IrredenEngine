# One sun-shadow receiver model on voxel faces across cardinal yaws

## The model

For a point on a visible voxel face, sun visibility is one finite-geometry
query (`worldSurfaceSunShadowFactor`) taken at the continuous point on the
face that the output fragment covers. Every fragment of the face takes its own
query. This holds at a cardinal camera yaw and between cardinals:

| Camera yaw | Voxel storage | Where the query runs | Point |
|---|---|---|---|
| Cardinal | single canvas, micro-faces | lit composite (`ir_trixel_to_framebuffer_body`) | `cardinalVoxelSurfacePoint` |
| Residual yaw | three per-axis canvases | scatter fragment (`ir_peraxis_scatter_fragment_body`) | `perAxisFaceClosestPoint` |

Both call `worldSurfaceLighting`, which varies only sun visibility across a
face. Ambient occlusion, the local-light sample and the material are taken once
per face on either side of the crossing.

The switch is `C_PerAxisSurfaceLighting::enabled_`, unset by default. Unset,
it is on for a pipeline that does not register `FOG_TO_TRIXEL` and off for one
that does (`IRPrefab::PerAxisSurfaceLighting::enabledFor`). An explicit value
applies to every pipeline: `IR_PERAXIS_SURFACE_LIGHTING=1` or `setEnabled(true)`
admits the surface routes on a fog-pass pipeline as well, and
`IR_PERAXIS_SURFACE_LIGHTING=0` or `setEnabled(false)` selects the per-face
sampled receiver everywhere: while rotating, one query at the face centre
(`perAxisSunShadowFactor`) whose result the lighting kernel applies to the
whole face. That receiver is kept for comparisons and as the fallback below.

## Staged plan

The default is one model on pipelines **without** a fog pass. That is stage 1.
A pipeline that registers `FOG_TO_TRIXEL` keeps the compute default at both yaw
classes (one query per stored micro-face at cardinal, one per base-resolution
face while rotating) until stage 2,
[#5123](https://github.com/jakildev/IrredenEngine/issues/5123), flips it with
its own moved-row inventory, `light-verify` re-bless and million-control cost
gate. The fog-pass demos (`perf_grid`, `lua_perf_grid`, `fog_demo`,
`random_voxels`, `skeletal_demo`, `chunk_streaming_smoke`, `scene_reset`,
`persist_roundtrip`, `reposition_stress`, `IRLightingCombined`,
`IRCanvasStress --fog-reveal`) therefore render as they did before stage 1.

The per-axis design's earlier resolution, "no fragment-rate lighting", is
superseded for sun visibility on voxel faces: the query is geometric and exact
at the fragment's own point, and the canvases still carry the geometry at
trixel resolution. No blur, bias, inflated coverage, averaged normal or
temporal blend is involved.

## Fallback routes

`IRPrefab::PerAxisSurfaceLighting::resolveRoute` decides per frame from that
frame's inputs. These frames light in compute, per stored face, at both yaw
classes:

| Condition | Why |
|---|---|
| A debug overlay or depth-colour mode is active | The overlay is the product; presentation must not relight it |
| The main canvas lacks AO, sun-shadow or light-volume resources, or the pipeline has no `COMPUTE_SUN_SHADOW` / `TRIXEL_TO_FRAMEBUFFER` | Incomplete lighting pipeline |
| A fog pass is registered and a source is line-of-sight gated | The gated source keeps its compute march |
| A fog pass is registered and a glitch subject is active | The composite does not fog the glitch presentation |
| A fog pass is registered and the switch is unset | Stage 1 default; stage 2 (#5123) removes it |
| The switch is off | Comparison arm |

Without a fog pass, an active cardinal glitch subject keeps the continuous
receiver on unaffected pixels (`cardinalGlitch_`). The overflow lane draws
through the same scatter fragment body as the owning faces, so it follows the
model on the surface routes and its own kernel (`c_light_overflow_faces`) on
the fallbacks. Fog composes once: in the composite on the surface routes
(`SurfaceFogRoute::COMPOSE`), in compute on the fallbacks.

Route tests: `scripts/tests/test_render_per_axis_surface_lighting.py`,
`scripts/tests/test_render_cardinal_surface_lighting.py` and the
`SurfaceFogRoute.*` / `PerAxisSurfaceLightingTest.*` /
`PerAxisSurfaceLightingDefault.*` cases in
`test/render/per_axis_surface_lighting_test.cpp`. Each harness runs the switch
unset on a fog-pass pipeline (compute fallback) and on a fog-free one (surface
route), and fails when `enabledFor` ignores the fog pass.

## Measurements

Metal, Apple M4 Max, macOS, Debug. The parent is the commit this change is
stacked on, captured in the same session as the head. The receiver-polygon and
alternative-cost readings were taken on battery, the rest on mains power.

### Crossing continuity

`scripts/render-yaw-lone-frame-metric.py` at its default `--max-ratio 1.3` on
`IRShapeDebug --spin-yaw --zoom 16 --auto-screenshot 720`:

| Yaw | Parent, default | Parent, continuous route on | Head, default |
|---|---:|---:|---:|
| 0° | 1.158 | 0.864 | 0.864 |
| 90° | 1.065 | 0.996 | 0.996 |
| 180° | 1.148 | 0.979 | 0.979 |
| 270° | 1.308 (fail) | 1.051 | 1.051 |

The sweep's four cardinal frames are byte-identical between parent and head.

`scripts/render-yaw-crossing-metric.py capture` (a rigid cube over a GRID
floor, camera yaws −0.5° to +0.5° in 0.25° steps), differing shadow-mask pixels
on the receiver's top face:

| Configuration | −0.5° / −0.25° | −0.25° / 0° | 0° / +0.25° | +0.25° / +0.5° | Result |
|---|---:|---:|---:|---:|---|
| Parent, default | 0 | 896 | 896 | 0 | FAIL |
| Parent, continuous route on | 32 | 24 | 16 | 16 | PASS |
| Head, default | 32 | 24 | 16 | 16 | PASS |

On the parent the rotated frames agree with each other (whole faces) and the
cardinal frame differs from both by 896 pixels: the model switch.

### Receiver correctness

The receiver-polygon controls (`docs/evidence/receiver-shadow-polygon/README.md`)
on the head, default configuration: the four intermediate yaws read 0 missing
and 0 excess pixels outside the one-pixel band, on the per-axis surface route
(they read 18 to 32 missing and 638 to 650 excess on the sampled route). The
four cardinal yaws pass with the switch unset, `0` and `1`.

### Moved rows

`render-verify` for `IRShapeDebug` and `IRCanvasStress`, head against a
same-session parent run on the same macOS references: every row whose camera
holds a cardinal yaw reads the same figures on both. The rows that moved are
rotating-camera frames, and the difference is the shadow edge on voxel
receivers following the face instead of stepping a whole face:

- `IRShapeDebug`: `zoom4_yaw45_inter_cardinal`, `zoom4_pan16_yaw45_pivot`
  (also under the two glitch-off passes), `sdf_voxel_pose_yaw45`,
  `sdf_voxel_pose_zoom2` (root and parented passes) and
  `lod_scene_medium_yaw45`.
- `IRCanvasStress`: the six default shots and `compare_yaw0` /
  `compare_yaw_q`, all taken with the camera auto-rotating.

Those thirteen macOS references are re-blessed from the head; the Windows and
Linux copies of the same rows are stale until those hosts re-capture them.

`light-verify` (`IRLightingEmissive`, fog-free) passes every row on both. Its
yaw-0 rows and hover sweep read the same figures; 22 of its 24 yaw-30 and
yaw-45 rows moved and stay inside the gate, so no baseline is re-blessed.

Fog-pass pipelines are unchanged: `render-verify --target IRFogDemo` reads the
same figures on all 66 rows, parent and head, and `IRLightingCombined
--auto-screenshot 5` captures are byte-identical.

### Not yet measured

- The cost gate: Release, five interleaved parent/head pairs of the million
  control and the fog-free surface-lighting matrix under a benchmark lease.
- OpenGL: every reading here is Metal.
- Pipelines that register a fog pass: unchanged by stage 1. Stage 2 (#5123)
  measures them.

### Alternatives measured

One diagnostic round per arm of `scripts/perf/surface_lighting_controls.py
--adoption-matrix` (360 frames, GPU stage means in ms). These are single Debug
rounds on a busy host: they rank the alternatives and are not the cost gate.

| Case | Sampled: sun compute + lighting + scatter | Continuous: sun compute + surface draw | Continuous, shadows off |
|---|---:|---:|---:|
| Per-axis, zoom 4, 16 overlapping casters | 0.131 + 0.042 + 0.505 | 0.014 + 0.957 | 0.019 + 0.900 |
| Per-axis, zoom 4, no overlap | 0.243 + 0.098 + 0.190 | 0.066 + 0.606 | not run |
| Per-axis, zoom 4, 8 overlapping casters | 0.188 + 0.061 + 0.160 | 0.020 + 0.669 | not run |
| Per-axis, zoom 1, 8 overlapping casters | 0.246 + 0.055 + 0.184 | 0.017 + 1.717 | not run |

Steady frame means in the same rounds: 10.05 ms sampled against 9.91 ms
continuous at zoom 4 with 8 casters, 10.14 against 10.85 ms at zoom 1.

- **Continuous route as the rotating default (chosen).** It is the cardinal
  receiver's own model, so the crossing gates pass by construction, and it adds
  no new mechanism.
- **Per-face verdict with per-fragment refinement** (classify a face as
  uniformly lit in compute, query per fragment only elsewhere). Not built. The
  shadows-off arm shows the query is about 0.06 ms of the 0.96 ms surface draw
  in the crowded case, so skipping queries on provably lit faces would recover
  little; the draw's cost is the per-fragment compose and raster.
- **Making the cardinal frame per-face.** Rejected: it trades the
  discontinuity for a worse still image (the polygon controls' 18 to 32 missing
  and 638 to 650 excess pixels).
- **Hoisting the per-face terms out of the fragment** (lit and shadowed colours
  per face, visibility per fragment). The remaining lever if the cost gate
  needs one; exact only for visibility 0 or 1, so fractional samples would
  still take the full compose.

The frame figures in `docs/perf/capacity-envelope/README.md` (9.12 ms sampled
against 18.00 ms continuous) predate the offscreen pacing change that stopped a
late Metal frame from sleeping one refresh interval, and must not be read as
the current cost.

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
it is on for every pipeline (`IRPrefab::PerAxisSurfaceLighting::enabledFor`),
whether or not the pipeline registers `FOG_TO_TRIXEL`.
`IR_PERAXIS_SURFACE_LIGHTING=0` or `setEnabled(false)` selects the per-face
sampled receiver everywhere: one query per stored micro-face at a cardinal yaw
and, while rotating, one query at the face centre (`perAxisSunShadowFactor`)
whose result the lighting kernel applies to the whole face. That receiver is
kept for comparisons and as the fallback below.

## Fog-pass pipelines

A pipeline that registers `FOG_TO_TRIXEL` takes the same two routes as one
that does not. Fog still applies once per pixel, and
`IRPrefab::PerAxisSurfaceLighting::surfaceFogRoute` says where:

| Main canvas | Route | Where fog applies |
|---|---|---|
| Carries fog, no line-of-sight gated source, no active glitch subject | `COMPOSE` | The lit composite fogs each surface-lit sample; `FOG_TO_TRIXEL` leaves those samples alone |
| Carries no fog (fog lives on other canvases) | `NONE` | Nothing fogs the main canvas |
| Carries fog and a source is line-of-sight gated, or a glitch subject is active | `FALLBACK` | `FOG_TO_TRIXEL`, on compute-lit colour |

The pipelines this covers in the tree are `perf_grid` (the million control),
`lua_perf_grid`, `fog_demo`, `random_voxels`, `skeletal_demo`,
`chunk_streaming_smoke`, `scene_reset`, `persist_roundtrip`,
`reposition_stress`, `IRLightingCombined` and `IRCanvasStress --fog-reveal`.
Their cardinal frames changed along with their rotating ones when the default
moved: a cardinal frame used to take one query per stored micro-face and now
takes one per fragment.

The default arrived in two stages. Stage 1 covered pipelines without a fog
pass. Stage 2 ([#5123](https://github.com/jakildev/IrredenEngine/issues/5123))
covered the pipelines above, together with the cost mechanism below. The
measurements keep that split, because each stage was read against its own
parent.

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
unset on a fog-pass pipeline and on a fog-free one. On the fog-pass pipeline
it has one case per route: `COMPOSE`, `NONE`, and `FALLBACK` for a gated
source and for a glitch subject. Each harness fails when the unset switch
follows the fog pass again.

### Fixtures that read compute-route intermediates

A probe that asserts on what `FOG_TO_TRIXEL` or the sampled sun-shadow pass
wrote into the main canvas cannot read a surface route, which leaves those
writes to the composite. `IRFogDemo` handles its probes two ways.

Five modes pin the switch off after `initSystems()`, so their rows are
comparison-arm rows on the compute route:

| Mode | What its probe reads |
|---|---|
| `--channel-probe` | Fog-painted canvas colours per vision channel |
| `--world-pan` | Fog-painted canvas colours across window moves |
| `--explored-decay` | Fog-painted canvas and overflow-lane colours |
| `--ghost-policy` | Explored-style canvas colours of ghost-held subjects |
| `--ceiling-shadow-curved` | Sampled sun-shadow factors under the fog rim, where the composite shows no shadow to read |

`--ceiling-shadow` is not pinned, because it guards a shadow the player sees.
On a surface route the canvas sun-shadow texture holds no query for a cardinal
voxel surface, so its probe reads the composited frame. It draws one warmup
frame with sun shadows off and the next with them on, and counts a receiver
pixel as shadowed when the two frames differ there. A pixel fog has darkened
to nothing reads as lit, so fog cannot stand in for a shadow. With the switch
off the probe reads the sampled factors as before. `--no-sun-shadows` is the
control arm: both frames match and the probe fails on the cut cap's missing
shadow.

## Stage 1 measurements (pipelines without a fog pass)

Metal, Apple M4 Max, macOS, Debug. The parent is the commit stage 1 was
stacked on. The parent columns of the crossing tables, the receiver-polygon
controls and the alternative costs were read on battery in an earlier session;
the head crossing readings were repeated on mains power with the same figures,
and every moved-row comparison pairs head and parent in one session.

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
on the receiver's top face, read on a macOS Metal 2560×1440 capture (output
scale 2). The grader derives the receiver region and its coverage floors from
the capture's output scale, so a 1x host's 1280×720 frames grade the same
scene at about a quarter of these counts:

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

Stage 1 left fog-pass pipelines as they were: `render-verify --target
IRFogDemo` read the same figures on all 66 rows on its parent and its head,
and `IRLightingCombined --auto-screenshot 5` captures were byte-identical.

### Cost

Release, default configuration on both arms (no `IR_PERAXIS_*` variable set),
five interleaved parent/head pairs per case, every run `QUIET=guarded`. Data,
driver and per-row tables: `docs/perf/voxel-receiver-yaw-continuity/`. Frame
rows pass the 1.10 point gate, which is rule (i) of
`docs/design/gpu-stage-timing-cost-model.md` § "State the noise bound":

- Fog-free surface-lighting matrix (`IRCanvasStress` shadowbox, 20 cases): the
  highest frame ratio on a case that changes route is 1.052 (per-axis, zoom 1,
  no overlap). The highest in the whole matrix is 1.069 (steady 1.095), and it
  is on a cardinal case whose route is the same on both arms.
- Million control (`IRPerfGrid`, fog pass, the no-change arm): 0.969 to 1.052
  over the six cases. Both arms register the same GPU stage set.

The surface draw does cost GPU time that the paced frame hides. Per-axis, zoom
1, no overlap: `perAxisSurfaceFb` reads 2.21 ms where the parent spent 0.26 ms
in scatter and 0.10 ms in compute lighting. The GPU envelope goes from 3.91 to
5.23 ms, while the frame moves only from 8.71 to 9.16 ms. With overlapping
casters the per-axis envelope reads 0.94 to 1.05 times the parent. On the lit
per-axis cases at base subdivision, `computeSunShadow` falls from 0.13–0.30 ms
to 0.01–0.06 ms, and at subdivision 4 from 0.32–0.39 ms to 0.18–0.20 ms.
Stage 2 met the GPU-bound case on the million control and found the cost in
the sun query on occluded fragments ([cost mechanism](#cost-mechanism)).

### Not yet measured

- OpenGL cost: every timing here is Metal. The correctness readings are taken
  on OpenGL in `docs/evidence/receiver-model-gl/README.md`. The polygon and
  crossing counts match the Metal figures above to the pixel wherever both hosts
  read the same case. The lone-frame ratios differ by host (1x frames against
  2x) and pass on both. The crossing reading at 90° and 270°, which Metal has
  not taken, fails on the continuous route from shadow-edge motion with no
  lone-frame pixel (#5184).

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
  per face, visibility per fragment). Not built; exact only for visibility 0
  or 1, so fractional samples would still take the full compose. Stage 2 read
  the surface draw at parity with the sampled route once shadows are off on
  the million control, so on that scene it has nothing to recover.

The frame figures in `docs/perf/capacity-envelope/README.md` (9.12 ms sampled
against 18.00 ms continuous) predate the offscreen pacing change that stopped a
late Metal frame from sleeping one refresh interval, and must not be read as
the current cost.

## Stage 2 measurements (fog-pass pipelines)

Metal, Apple M4 Max, macOS, mains power. The parent is the commit stage 2 is
stacked on, built in its own worktree; every comparison pairs head and parent
captures from one session. Captures are Debug, timings Release.

### Crossing continuity

`scripts/render-yaw-crossing-metric.py capture --fog-reveal 16`, the stage 1
fixture with the fog pass registered and the whole graded receiver revealed,
2560×1440. No `IR_PERAXIS_*` variable is set unless the row names one:

| Configuration | −0.5° / −0.25° | −0.25° / 0° | 0° / +0.25° | +0.25° / +0.5° | Result | Logged fog route, all five frames |
|---|---:|---:|---:|---:|---|---|
| Parent, default | 0 | 968 | 968 | 0 | FAIL | `fallback` |
| Parent, `IR_PERAXIS_SURFACE_LIGHTING=1` | 32 | 24 | 16 | 16 | PASS | `compose` |
| Head, default | 32 | 24 | 16 | 16 | PASS | `compose` |
| Head, `IR_PERAXIS_SURFACE_LIGHTING=0` | 0 | 968 | 968 | 0 | FAIL | `fallback` |

The fog-free arm reads 32 / 24 / 16 / 16 and PASS on the head, as it did
after stage 1.

### Receiver correctness

The receiver-polygon capture set
(`docs/evidence/receiver-shadow-polygon/README.md`) re-captured with
`--fog-reveal 16` in the default configuration and graded by
`scripts/render_receiver_shadow_polygon.py`. Counts are pixels outside the
one-pixel band; no case has an unknown-colour pixel:

| Camera yaw, radians | Parent missing / excess | Parent route | Head missing / excess | Head route |
|---|---:|---|---:|---|
| 0, π | 60 / 52 | `fallback` | 0 / 0 | `compose`, cardinal |
| π/2, 3π/2 | 64 / 59 | `fallback` | 0 / 0 | `compose`, cardinal |
| 0.6, π+0.6 | 18 / 638 | `fallback` | 0 / 0 | `compose`, per-axis |
| π/2+0.6, 3π/2+0.6 | 32 / 650 | `fallback` | 0 / 0 | `compose`, per-axis |

The cardinal rows are the ones stage 2 had to show at least as correct: a fog
pipeline's cardinal frame took one query per stored micro-face, which the
oracle fails by 112 to 123 pixels, and now takes one per fragment.

### Exactness

Stage 2 changes which model is the default and what it costs. It does not
change either model's pixels. For all 103 `render-verify` rows of
`IRPerfGrid`, `IRLuaPerfGrid` and `IRFogDemo`, the six `IRLightingCombined
--auto-screenshot 10` frames and the five crossing frames:

- The head with `IR_PERAXIS_SURFACE_LIGHTING=0` is RGB-identical to the
  parent's default on every row (109 of 109, and the five crossing frames).
- The head's default is RGB-identical to the parent with
  `IR_PERAXIS_SURFACE_LIGHTING=1` on every row the parent can capture that
  way (104 of 104, and the five crossing frames). The parent cannot capture
  five: its `--ceiling-shadow` and `--ghost-policy` probes read compute-route
  intermediates and exit before the shot. The two `--ceiling-shadow` rows are
  identical to a parent build with that probe unregistered. The three
  `--ghost-policy` rows are pinned to the compute route on the head and are
  identical to the parent's default.

### Moved rows

Head default against a same-session parent default, 109 rows. 41 moved:

- `IRPerfGrid` (14 rows): all seven shots of the `static_cycle` and
  `static_cycle_dynamic` passes, four cardinal and three rotating in each.
  The 21 rows of the other three passes did not move.
- `IRLuaPerfGrid` (2 rows): `fit_grid`, `zoom1_origin`, both cardinal.
- `IRFogDemo` (19 rows): `fog_ceiling_shadow9`, `fog_ceiling_shadow_yaw9`,
  `fog_ceiling_treatment_yaw9`, `fog_detached_edge_zoom5`,
  `fog_detached_edge_zoom9`, `fog_detached_edge_yaw90_zoom5`,
  `fog_detached_field_hidden`, `fog_edge_zoom5`, `fog_edge_zoom9`,
  `fog_edge_zoom14`, `fog_edge_sdf_blocker_zoom5`,
  `fog_edge_sdf_blocker_zoom9`, `fog_edge_smooth5`, `fog_edge_smooth9`,
  `fog_edge_smooth14`, `fog_edge_zcost_ceiling_paint_yaw9`,
  `fog_entity_reveal`, `fog_entity_reveal_soft`, `fog_peraxis_overflow_yaw9`.
  The other 47 rows did not move; the line-of-sight (`occlusion*`) rows and
  the five pinned modes stay on the compute route.
- `IRLightingCombined` (6 frames): every frame. `light-verify` has no macOS
  baseline for this demo.

Two kinds of pixel move. Sun-shadow edges on voxel receivers follow the
fragment instead of the stored face (channel deltas up to 123). And where
fog now composes in the composite, the fogged lit colour rounds once instead
of twice, a delta of exactly 1: of the 136,468 pixels that move in
`fog_ceiling_shadow9`, 128,048 move by 1 and 8,420 are shadow edge.

The 28 macOS references behind the 35 moved `render-verify` rows are
re-blessed from the head (the two `IRPerfGrid` cycle passes share seven
files). The `windows-debug` copies of 18 of the `IRFogDemo` rows are stale
until a Windows host re-captures them: every moved row above except
`fog_detached_field_hidden`, which has no Windows reference. `perf_grid` has
no `windows-debug` copy of a moved row, `lua_perf_grid` has no Windows set,
and no demo here has a `linux-debug` set.

### Cost

<!-- STAGE2-COST -->

### Not yet measured

- OpenGL. Every stage 2 reading is Metal. The crossing reading on the
  fog-pass arm is owed on an OpenGL host; the fog-composing GLSL programs were
  compiled, linked and graded there before the default moved
  (`docs/evidence/fog-surface-lighting-gl-native/README.md`). The
  `depth_greater` declaration and the early-depth vertex output have not been
  compiled by a GL driver.
- `IRLightingCombined` through `light-verify` on macOS, which has no baseline
  for it.

## Cost mechanism

The surface routes take one sun query per fragment, and the scatter draws
every fragment of every face the rotated view could show. The million control
at 45° draws 6,378,421 fragments for 927,048 framebuffer pixels (the
`IR_PERAXIS_VISIBILITY_STATS=1` counters, Debug). On the stage 2 parent that
is where the whole cost of the route sits:

<!-- STAGE2-MECHANISM -->

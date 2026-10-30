# CanvasStress mode-switch, detach and impulse mismatches

All five inherited mismatches survive on master `469402034`, and one commit
causes all of them: #4615 `1a309a37d` ("sample cardinal shadows on continuous
receiver planes"). Every changed pixel is a sun-visibility toggle on a
GRID-resident (main-canvas, cardinal) voxel. The live mode-switch sequence
returns byte-identically to its starting frame, so the defect is static, not a
transient.

The change set has two classes. **Darker** pixels are one-trixel shadow islands
on face edges: the unlimited receiver point lands at or behind the neighbouring
voxel. This PR fixes them with the coplanar-neighbour edge rule. **Brighter**
pixels, and the darker pixels the rule leaves, are a change of sample point, not
an error. The references displayed one shadow sample, taken at the stored
micro-square centre, across the whole texel pair. The continuous receiver shows
each fragment's own point, so a shadow boundary that crosses a micro-square now
splits it. A double-precision CPU port of the finite sun query
([`tools/receiver_oracle.py`](tools/receiver_oracle.py)) agrees with the GPU at
every changed pixel of all five rows, on both routes. The five references are
refreshed on that attribution, and thresholds are unchanged.
[§ Head](#head-the-coplanar-neighbour-edge-rule) has the fix and its grading;
the sections before it are the parent (master) attribution.

Host: macOS arm64, Metal, `macos-debug` (Debug). Each capture is a fresh
`fleet-run` process with the manifest's argv ([`tools/cap.py`](tools/cap.py)).
Every run exited `RESULT=CLEAN`. [`results.json`](results.json) holds every
hash, verdict, comparator output and pixel class.

## Current master

`render-verify --target IRCanvasStress --no-build` on `469402034` fails 7 of 46
checks ([full log](master-render-verify.log.gz)):

| Row | Master | Match % | Max Δ | Changed px |
|---|---|---:|---:|---:|
| `modeswitch_grid` | FAIL | 99.9849 | 107 | 740 |
| `modeswitch_grid_return` | FAIL | 99.9849 | 107 | 740 |
| `detach_grid` | FAIL | 99.9891 | 110 | 536 |
| `impulse_midspin` | FAIL | 99.9588 | 96 | 2,024 |
| `impulse_settled` | FAIL | 99.9655 | 96 | 1,696 |
| `compare_yaw0`, `compare_yaw_q` | FAIL | 99.9992 | 240 | — (unrelated, below) |

None of the five has been fixed by a later merge. #4721, #4733, the glitch
presentation repair, #4777 and #4784 leave all five frames byte-identical to
`e9c594cd2`, the first revision measured red. The other 39 checks pass,
including all 16 cylinder receivers.

## Causal controls

| Build | Five rows vs reference | Five rows vs master |
|---|---|---|
| `378a00f7d` (first parent before #4615) | byte-identical | differ |
| `1a309a37d` (#4615) | differ | byte-identical |
| `e9c594cd2` | differ | byte-identical |
| master + [cardinal gate forced off](patches/control-cardinal-off.patch) | byte-identical | differ |

The window starts at #4590 (`c30f3ac35`), whose re-bless run passed all 46.
#4615 is one first-parent step after `378a00f7d`. The gate-off control changes a
single boolean, `cardinalSurfaceLightingEligible_`. On current master it
restores all nine frames of the three scenes exactly, so nothing else since the
references were blessed has moved these rows.

## Geometry, normals, depth ownership, shadow

Per-pixel classes, master vs reference ([`tools/pairs.py`](tools/pairs.py)):

- **Geometry and depth ownership unchanged.** No changed pixel involves the
  background. Object pixel counts are identical (10,880 / 125,128 / 164,480 /
  162,304). Every changed pixel keeps its albedo: 100% are a single scalar
  multiple of the reference colour. The pixel therefore still belongs to the
  same entity and face.
- **Normals unchanged.** Every lit value is one of the face's existing Lambert
  levels (e.g. 169/178/146 for the orange prism). Every dark value is the
  ambient-only level, 0.30 × albedo. Only sun visibility differs.
- **Only main-canvas GRID voxels change.** These are the orange GRID prism, the
  blue part detached to GRID, and the cyan GRID impulse solid. The detached
  re-voxelized solids, hosted parts and own-canvas parts are untouched, which
  matches #4615's eligibility predicate.
- **Shadow projection changes at face edges.** The new dark pixels are mostly
  isolated 2×2 framebuffer blocks, one trixel each, on the boundary between two
  faces of the same solid
  ([edge zoom](crops/impulse_settled_edge_zoom.png): reference | master |
  changed). Most of them come from the unlimited receiver point. A true shadow
  tip can also be one trixel wide on the continuous route, so the head grades
  each one with the CPU oracle rather than assuming every speckle is false.

| Row | Edge speckles, reference | Edge speckles, master | Diagnostic clamp |
|---|---:|---:|---:|
| `modeswitch_grid` | 0 | 52 | 24 |
| `detach_grid` | 0 | 83 | 21 |
| `impulse_midspin` | 0 | 301 | 57 |
| `impulse_settled` | 0 | 268 | 10 |

Edge speckles are counted by
[`scripts/render-shadow-speckle-metric.py`](../../../../scripts/render-shadow-speckle-metric.py).
These are components of at most one trixel that are shadowed against a lit
same-albedo neighbour and whose ring touches two distinct lit face levels.
The metric measures the trixel pitch from the capture (2 px a side here), so
a 1x Windows/OpenGL capture reads the same count: nearest-neighbour 1x and 3x
copies of every gated reference and retained master frame match the 2x count
exactly (`scripts/tests/test_render_shadow_speckle_metric.py`). A cutoff fixed
at 4 px reads 50 / 39 / 94 / 27 on the 1x references.
The passing control rows (`modeswitch_revox`, `detach_own_canvas`,
`sharedparts_spin_a`) read 0.

Crops: [mode switch](crops/modeswitch_grid.png),
[detach](crops/detach_grid.png), [impulse](crops/impulse_settled.png).
Full master frames are under [`master/`](master/). `modeswitch_grid_return`
hashes identically to `modeswitch_grid`.

### Mechanism

`cardinalVoxelSurfacePoint` intersects the fragment ray with the stored face
plane. On master it never limits the point to the owning micro-square. At a
face edge, rectangular storage assigns a fragment to a face whose plane
intersection falls outside that face. The point lands at or inside the
neighbouring voxel, and the finite shadow query reports it occluded.

[Clamping to the micro-square](patches/diag-clamp-micro-square.patch), Metal
only, was the first diagnostic: it removes 54–96% of the edge speckles but
leaves every row red. The architect direction on the PR rules out the
unconditional clamp, because it tears a flat run into per-micro-square samples.
It also explains why the clamp leaves the rows red: no brighter pixel moves
under it.

## Head: the coplanar-neighbour edge rule

Per in-plane axis of the stored slot, a receiver point within the micro-square
(plus `1e-4`) stays. Past it, the gather reads the texel one micro-step along
that axis from the owner texel. If that texel stores the same slot and flip at
depth `owner ± 1` with the cardinal-voxel marker, it is a coplanar
continuation and the extrapolation stays. Otherwise the point is clamped to the
micro-square on that axis. GLSL and Metal are line-for-line parallel
(`ir_cardinal_voxel_receiver.{glsl,metal}`). The rule is documented in
`docs/design/surface-shadow-sampling.md` § "Cardinal presentation receivers".

`test_render_cardinal_voxel_receiver.py::test_fragment_ray_intersects_the_published_plane`
stages the four in-plane neighbours of every published micro-square, on both
backends. Arm 0 stages coplanar continuations, and the point must equal the
unlimited ray/plane intersection. Arms 1–6 must clamp on the overshooting axis:
neighbour absent, wrong slot, wrong flip, one plane off, no voxel marker, off
canvas. A producer check pins the probe step: the next micro-square along each
in-plane axis publishes one iso step on, at the same half, one depth unit
deeper. Ten mutants fail: no pixel-centre shift, off-plane point, unconditional
clamp, probing the opposite neighbour, unconditional extrapolation, and ignoring
slot, flip, depth, provenance or the canvas bounds.

### Parent vs head

Head frames are the refreshed references
(`creations/demos/canvas_stress/test/references/macos-debug/<row>.png`). The
parent frames are [`master/`](master/), and the pre-fix references are
`origin/master`'s copies of the same files. Pixel classes are against the
pre-fix references ([`tools/census.py`](tools/census.py), screenshot pixels).
Edge speckles come from `render-shadow-speckle-metric.py`, full frame.

| Row | Master darker / brighter | Head darker / brighter | Edge speckles: pre-fix ref / master / head |
|---|---:|---:|---:|
| `modeswitch_grid` | 324 / 416 | 104 / 416 | 0 / 52 / 24 |
| `modeswitch_grid_return` | 324 / 416 | 104 / 416 | 0 / 52 / 24 |
| `detach_grid` | 324 / 212 | 48 / 212 | 0 / 83 / 21 |
| `impulse_midspin` | 1,328 / 696 | 152 / 696 | 0 / 301 / 57 |
| `impulse_settled` | 1,440 / 256 | 140 / 256 | 0 / 268 / 10 |

[Edge crop](crops/head_edge_zoom.png), `impulse_settled`: pre-fix reference |
head | master | reference ≠ head. Master's dotted edge islands are gone at the
head; what changes sits along the shadow band boundaries.

On these frames the rule reproduces the diagnostic clamp's darker counts
exactly. Every edge that leaves a darker pixel here is non-coplanar, and the
brighter class does not move. `modeswitch_detached`, `modeswitch_revox`,
`detach_hosted` and `detach_own_canvas` are byte-identical to their references
at the head.

### CPU shadow oracle

IRCanvasStress has a capture-only `--cardinal-receiver-probe`
(`engine/prefabs/irreden/render/cardinal_receiver_probe.hpp`). On each
screenshot frame it dumps the main canvas distance, entity-id and colour texels
with the gather's pixel-to-canvas placement. `--sun-face-geometry-probe` dumps
the frame's lossless sun face records. Probe-on frames are byte-identical to
probe-off frames. For each framebuffer pixel that differs between the pre-fix
reference and the head, the oracle reads the texel the gather reads and evaluates
the finite query at three points. `centre` is the stored micro-square centre,
where the references sampled. `ray` is the fragment ray / plane intersection,
solved from the iso equations independently of the shader helpers, which is
master's sample point. `limited` is `ray` under the head rule.

Mapping check: 0 of 2,720 / 6,848 / 19,776 / 18,976 canvas-covered samples
(modeswitch / detach / midspin / settled) map to background. All records are
faces; none is an analytic shape.

| Row | Class | Pixels | Components (px) | Ref = CPU at centre | Head = CPU at limited | Master = CPU at ray | Edge-limited | Micro-square shadowed share | Marginal |
|---|---|---:|---:|---:|---:|---:|---:|---|---:|
| `modeswitch_grid` | darker | 26 | 17 (1–4) | 26 | 26 | 26 | 15 | all in (0, 0.5] | 0 |
| | brighter | 104 | 52 (1–9) | 104 | 104 | 104 | 8 | all in (0.5, 1) | 0 |
| `detach_grid` | darker | 12 | 11 (1–2) | 12 | 12 | 12 | 2 | all in (0, 0.5] | 0 |
| | brighter | 53 | 21 (1–5) | 53 | 53 | 53 | 2 | all in (0.5, 1) | 0 |
| `impulse_midspin` | darker | 38 | 30 (1–8) | 38 | 38 | 38 | 2 | all in (0, 0.5] | 1 |
| | brighter | 174 | 61 (1–6) | 174 | 174 | 174 | 11 | all in (0.25, 1) | 0 |
| `impulse_settled` | darker | 35 | 35 (1) | 35 | 35 | 35 | 1 | all in (0, 0.5] | 0 |
| | brighter | 64 | 36 (1–4) | 64 | 64 | 64 | 4 | all in (0.25, 1) | 0 |

(Pixels are framebuffer pixels, each 2×2 in the capture; `modeswitch_grid_return`
grades identically to `modeswitch_grid`. Per-component rows are in
[`oracle/`](oracle/). `Marginal` counts pixels whose CPU answer flips when every
face grows or shrinks by 1e-3 or the depth window moves by 1e-3 voxels. The
shadowed share is an 8×8 in-plane sampling of the pixel's micro-square.)

Readings:

- **The GPU is right at every point it samples, on both routes.** References
  match the CPU at the centre, master matches it at the unlimited ray point, and
  the head matches it at the limited point, for 100% of changed pixels. The
  oracle reconstructs the fragment point without the shader's helpers, so a
  receiver reconstruction defect would show up as a disagreement. None does.
- **Brighter pixels are reference over-darkening, not lost shadow.** Their
  micro-squares are partly shadowed, never fully. The reference darkened the
  whole texel pair because its centre sat inside the shadow. The head lights only
  the fragments whose own points are lit. Symmetrically, darker survivors sit on
  partly shadowed micro-squares whose centre was lit.
- **No pixel contradicts its own micro-square.** No lit head pixel sits on a
  fully shadowed micro-square, and no shadowed one on a fully lit micro-square.
- **The surviving edge speckles are true shadow.** The oracle grades every pixel
  of every head edge speckle (`--speckles`): 24 / 24 / 21 / 57 / 10, all
  shadowed on the CPU and none marginal. The manifest's `shadow-speckle` gates
  sit at exactly those counts, so the oracle-confirmed false count is 0. Master's
  frames exceed every gate (52 / 83 / 301 / 268).
  `test_render_shadow_speckle_metric.py` asserts both directions against the
  manifest.

Not run: the architect's per-axis `--yaw 0.01` control. A yawed capture shifts
every pixel against these references, and the oracle grades the brighter class
directly at each point.

## Live sequences

All three manifest scenes are single live processes, not fixed-pose restarts.
The mode switch runs GRID → DETACHED → DETACHED_REVOXELIZE → GRID. The detach
scene moves parts from hosted to their own canvas to GRID. The impulse scene
goes from rest through a kick to spin and back to rest. Within the live
mode-switch run, `modeswitch_grid_return` is byte-identical to `modeswitch_grid`
on master, on #4615, and in the reference. A full round trip through two
detached routes leaves no residue, so the defect is the cardinal receiver
itself, not a transition. The DETACHED and REVOX frames in that sequence are
byte-identical to their references.

## Backends

- **Metal** is graded above: pixel references plus the reference-free
  `shadow-speckle` structural gate on the five rows.
- **Windows/OpenGL** runs the same gate wherever `render-verify` runs, because
  the gate needs no reference. Grading these rows on a Windows host is #4807's
  job. The GLSL twin of the rule runs through the same C++ contract harness as
  Metal (`render-harness-tests.yml`).

## Unrelated failure, reported separately

`compare_yaw0` and `compare_yaw_q` fail on master with max Δ 240 and 99.9992%
match, independently of this work. They are tracked by #4806.

## Replay

```sh
T=docs/pr-screenshots/claude/4763-canvas-stress-mismatch-attribution/tools
python3 $T/cap.py --build-dir build --out /tmp/canvas-rows --scene impulse \
  --extra=--cardinal-receiver-probe --extra=--sun-face-geometry-probe  # also modeswitch, detach
# the probes write cardinal-receiver-<shot>.* and sun-face-index-<shot>.csv beside the exe
python3 $T/receiver_oracle.py --dump build/creations/demos/canvas_stress --shot 1 \
  --ref <pre-fix impulse_settled.png> --cap /tmp/canvas-rows/impulse/impulse_settled.png \
  --master docs/pr-screenshots/claude/4763-canvas-stress-mismatch-attribution/master/impulse_settled.png \
  --speckles
python3 $T/census.py <pre-fix ref.png> <capture.png>
python3 -m unittest scripts/tests/test_render_shadow_speckle_metric.py \
  scripts/tests/test_render_cardinal_voxel_receiver.py
```

Shots are 0-based in scene order (`impulse_midspin` 0, `impulse_settled` 1;
`detach_grid` 2; `modeswitch_grid` 0, `modeswitch_grid_return` 3). The parent
and #4615 controls were built in a detached scratch worktree with its own build
directory. Before each capture, every staged Metal shader was checked with `cmp`
against the checked-out source.

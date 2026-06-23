# Temporal-jitter rotation validation harness (#1922, epic #1881)

Engine-side procedure to **measure whether the rendered surface shimmers as the
camera turns** — the core artifact of epic #1881 (rotated-voxel correctness
under camera Z-yaw). The sibling solidity harness (`perf-grid-rotate-sweep`,
#1882) scores per-frame *coverage* of one rotated cube — it catches see-through
density loss but **not** temporal instability, and only on a single shape. This
harness sweeps every SDF shape across a fine continuous yaw rotation and scores
how much each pixel *crawls* frame-to-frame, so the epic's "no jitter" bar is a
measured number rather than an eyeball judgment.

The jitter children (#1920 round-to-cell depth bands, #1883 per-axis seams)
validate their fixes against this harness: a shape that FAILs the gate on
`origin/master` should pass once its fix lands.

## What the metric measures, and why curvature not speed

The artifact: under camera Z-yaw an interior pixel oscillates dark → light →
dark while the camera turns, instead of ramping smoothly. Two crawl modes
appear — per-axis trixel-scatter **seam crawl** on hard edges / flat faces at
grazing iso angles (box, wedge), and a concentric **depth-band crawl** on curved
SDF surfaces (#1920, a band flipping between adjacent integer depths) which is
the *stronger* of the two on master (curved_panel and ellipsoid). The stated
crux of the issue is separating either
crawl from the *expected* smooth sub-pixel motion every pixel undergoes as the
whole image rotates.

The discriminator is the **second temporal difference** of per-pixel luminance —
the discrete temporal Laplacian:

    d2[p,t] = | L[p,t-1] - 2·L[p,t] + L[p,t+1] |

A pixel whose luminance ramps *smoothly* with yaw has near-zero curvature
(`d2 ≈ 0`); a pixel that *oscillates* across an integer boundary spikes `d2`
every flip. The first difference `d1 = |L[p,t] - L[p,t-1]|` cannot tell them
apart — smooth motion drives `d1` just as hard — which is why the metric keys on
curvature. It is measured over the **interior** only (a pixel foreground in
*every* frame of the sweep), so the moving silhouette boundary and the
background don't contaminate the signal. Luminance curvature is the same
property on Metal and OpenGL, so a single gate is a shared cross-backend oracle.

`scripts/render-jitter-metric.py` implements the metric (pure stdlib, streaming
3-frame window); its header documents every reported field. The headline number
is `jitter_score` (mean interior `|d2|`); `flicker_p95` and `flicker_frac` catch
localized crawl that barely moves the mean.

## The automated sweep: `scripts/dev/shape-rotate-jitter-sweep`

```bash
bash scripts/dev/shape-rotate-jitter-sweep [build_dir] [shapes] [zoom] [steps]
#   build_dir  CMake build dir            (default: build)
#   shapes     space-separated subset     (default: all 8)
#   zoom       sweep zoom                 (default: 8)
#   steps      shots over one 360° turn   (default: 360 → 1°/step)
# Env: SPIN_RATE (deg/s, 30) · MAX_JITTER (gate, 4.0) · RENDER (sdf|voxel|both, sdf)
```

It drives `IRShapeDebug --spin-shape <name>`, which spawns a **single** shape
centred at the origin: under yaw-about-origin the shape stays screen-centred, so
one centred ROI crop tracks it across the whole sweep (the default side-by-side
fixture scene has shapes orbit off-centre, so no fixed crop would track one).
Each sweep shot writes that centred crop and the metric scores the small crops —
a full-framebuffer 360-frame sweep is otherwise minutes of pure-Python PNG
decode. `--spin-shape-voxel` renders the voxel-pool twin instead of the SDF
solver so both render paths can be scored (`RENDER=both`).

**A fine sweep is mandatory.** At coarse yaw steps even a smoothly-shaded
surface aliases into a high second-difference (sphere reads ~1.2 at 1°/step but
climbs steeply by 2°/step), drowning the real crawl signal. 1°/step (the default
360 steps) is the #1922 spec and the regime where smooth shapes settle to
near-zero flicker.

## Baseline (macOS / Metal, 2560×1440, origin/master renderer)

Measured at the default zoom 8, 360 steps (1°/step), 30 deg/s, SDF render path,
ROI = centred half of the tracking crop. Gate: `MAX_JITTER=4.0`.

| shape         | jitter_score | flicker_p95 | flicker_frac | verdict |
|---------------|--------------|-------------|--------------|---------|
| box           | 6.74         | 16.07       | 0.575        | **FAIL** |
| sphere        | 1.20         | 1.89        | 0.000        | PASS    |
| cylinder      | 1.17         | 1.82        | 0.001        | PASS    |
| ellipsoid     | 7.54         | 16.74       | 0.727        | **FAIL** |
| cone          | 1.13         | 1.73        | 0.003        | PASS    |
| torus         | 1.07         | 1.83        | 0.001        | PASS    |
| wedge         | 6.26         | 18.18       | 0.459        | **FAIL** |
| curved_panel  | 11.38        | 18.18       | 0.886        | **FAIL** |

### What the split proves

The gate cleanly separates two clusters with a ~5× margin: four shapes settle at
`jitter_score` ≈ 1.1 (torus 1.07, cone 1.13, cylinder 1.17, sphere 1.20) and
four spike to 6.3–11.4 (wedge 6.26, box 6.74, ellipsoid 7.54, curved_panel
11.38). Both of the epic's crawl modes are live on master:

- **Hard-edge seam crawl** (per-axis trixel-scatter): box and wedge — flat faces
  and sharp seams toggle as the iso projection grazes them.
- **Curved-surface depth-band crawl** (#1920): a concentric band flips between
  adjacent integer depths. This is the *stronger* mode on master — curved_panel
  crawls hardest (11.38, 89% of its interior flickering), ellipsoid next (7.54).
  curved_panel is the canonical curved-SDF band crawl the issue calls "Bug-A",
  reproduced here as the worst FAIL on origin/master.

The decisive evidence that the metric flags genuine crawl and not merely shape
class is that the **curved** family lands on *both* sides: sphere, cylinder,
cone, and torus pass cleanly while ellipsoid and curved_panel fail hard. An
edges-vs-curves classifier could not produce that split.

That the metric isolates crawl from *motion* — the issue's stated crux — is
proven separately and airtight by the unittest: a synthetic gradient sliding
1px/frame has a large first difference yet a near-zero second difference, so it
scores ≈0 jitter. On these master shapes motion and crawl happen to correlate
(the crawling shapes also have the larger `mean_abs_d1`: box 3.66, ellipsoid 4.13
vs sphere 0.82, cylinder 0.81), so the real-shape baseline alone cannot make the
motion-vs-crawl point — the synthetic control does.

> This corrects the original plan's offhand guess that box would be a stable
> control. Box is not a control — it FAILs (6.74) — and it is not even the worst
> case: the curved-SDF band crawl pushes curved_panel (11.38) and ellipsoid
> (7.54) above it. The clean shapes are the *smooth analytic* surfaces, not
> "curved shapes" as a class.

### Choosing the gate

`jitter_score = 4.0` sits in the wide empty band between the two clusters — the
noisiest passing shape is sphere at 1.20, the cleanest failing one is wedge at
6.26, leaving margin on both sides. It is a starting threshold for the fix
children, not a final regression bar — once #1920 (depth-band) and #1883
(per-axis seams) land, re-run this harness and tighten the gate to whatever
headroom the fixed renderer leaves (the curved-band shapes should drop the most;
every shape should fall well under 4.0).

### Render-path coverage and deferred baselines

`shape_debug --spin-shape` exercises the two render paths where the epic's crawl
artifacts live: the **SDF analytical solver** (default) and the **voxel-pool**
raster (`--spin-shape-voxel` / `RENDER=voxel|both`), which under camera yaw is the
per-axis trixel-scatter path (#1883). Both are on the main canvas. The committed
table above is the SDF path on Metal; the voxel-path numbers run from the same
one command (`RENDER=both`) and are captured the same way — box on the voxel path
already FAILs comparably to SDF (≈6.7), so the gate carries over, but the full
per-shape voxel table is a follow-up (it belongs next to the OpenGL baseline,
both being "run the harness on the other axis and paste the numbers").

The **detached / world-placed entity-canvas** path (SO(3) re-voxelize) is *not*
covered here: it carries its own round-to-cell sub-cell non-determinism (a few-
percent speckle that is not frame-to-frame stable even on a corrected renderer),
so a tight temporal-curvature gate would false-positive on it. That path is
validated by its own render-verify manifest; this harness deliberately scopes to
the deterministic single-shape main-canvas paths so the gate stays a hard oracle.

### OpenGL / Linux

The metric is backend-agnostic (luminance curvature), so the same gate applies
to OpenGL. This table is the **Metal** baseline (the authoring host); the
OpenGL/Linux baseline is captured by running the identical command on a Linux
host (cross-host smoke or a Linux worker) and is a follow-up — the renderer
behaviour, not the harness, is what differs between backends.

## Reproducing

1. `fleet-build --target IRShapeDebug`
2. `bash scripts/dev/shape-rotate-jitter-sweep` (all 8 shapes, the baseline above)
   - one shape: `bash scripts/dev/shape-rotate-jitter-sweep build box`
   - voxel-pool twin: `RENDER=both bash scripts/dev/shape-rotate-jitter-sweep`
3. Per-frame crops land in
   `build/creations/demos/shape_debug/save_files/screenshots/`; per-shape run
   logs in `/tmp/jitter-sweep-<shape>-<path>.log`. The script exits non-zero if
   any shape exceeds the gate.
4. To see *where* a shape crawls, score its crops directly with a heatmap:
   ```bash
   python3 scripts/render-jitter-metric.py <crops...> --roi X,Y,W,H --diff-out heat.png
   ```

The metric has a stdlib unittest (`scripts/tests/test_render_jitter_metric.py`)
that proves the discrimination on synthetic frames — smooth motion scores
near-zero, a crawling pattern scores an order of magnitude higher — so the
metric's core claim is guarded without a GL/Metal context.

## canvas_stress (mixed DETACHED + GRID): `scripts/dev/canvas-stress-rotate-jitter-sweep`

`shape-rotate-jitter-sweep` (above) scores a **single** centred shape on the main
canvas. The `canvas_stress` sibling extends the same metric to the **mixed
canvas-type** scene — detached re-voxelize SO(3) solids + GRID-spin cubes on the
screen-center column — under a fine continuous **camera Z-yaw** sweep. This is the
multi-canvas-type temporal coverage gap #1922 deferred (part of epic #1881), and the
re-scoped deliverable of #1954 (originally aimed at the #1942 DETACHED-vs-GRID
camera-yaw jitter, re-scoped to the general temporal-crawl family once #1942 was shown
to be a *spatial*-registration artifact owned by the #1944 render-verify gate, invisible
to this temporal metric by construction).

```bash
bash scripts/dev/canvas-stress-rotate-jitter-sweep [build_dir] [from_rad] [to_rad] [steps]
#   build_dir  CMake build dir                 (default: build)
#   from_rad   sweep start camera yaw, RADIANS (default: 0)
#   to_rad     sweep end camera yaw, RADIANS   (default: 0.2618 = 15°)
#   steps      sweep shots from→to             (default: 36 → ~0.42°/step)
# Env: WARMUP (settle frames, 60) · MAX_JITTER (gate, 4.0) · ROI_FRAC (central ROI, 0.5)
```

It drives `IRCanvasStress --sweep-yaw <from> <to> <steps> --no-spin
--auto-screenshot <warmup>`, which emits `steps` full-frame captures
(`screenshot_NNNNNN.png`, capture order = yaw order) at a fixed zoom-1, camera-(0,0)
framing with only the camera yaw advancing. The metric ROI-scopes to the central
half — the screen-center column where the detached + GRID solids sit (derived from the
frame's IHDR, zoom/host robust) — and the whole foreground column fits inside it, so
the interior is stable across the whole sweep.

**`--no-spin` is mandatory, not optional.** Two flags gate rotation in this demo:
`--sweep-yaw` freezes the *camera* auto-yaw (`autoRotate_`), but the *per-entity*
self-spin is gated separately by `noSpin_`. Without `--no-spin` the detached + GRID
solids keep self-spinning ~30° between the 60-settle-frame shots, and the metric — which
needs a fine ≤~2°/step sweep — aliases that coarse self-rotation into a high
second-difference, reading **jitter_score ≈ 3.7–4.4**. That number is *not* a speckle
floor; it is the entity self-spin confounding the camera-yaw measurement. Freezing the
entities isolates the actual camera-yaw crawl — the #1944 surface, and the same variable
`shape-rotate-jitter-sweep` measures.

### Baseline (macOS / Metal, 2560×1440, origin/master renderer)

Camera-yaw sweep 0→0.2618 rad (15°) across 36 shots (~0.42°/step), entities frozen
(`--no-spin`), central-half ROI. Gate: `MAX_JITTER = 4.0`.

| scene                         | jitter_score | flicker_p95 | flicker_frac | interior_px | verdict |
|-------------------------------|--------------|-------------|--------------|-------------|---------|
| canvas_stress (DETACHED+GRID) | 1.19         | 7.29        | 0.134        | 227 864     | PASS    |

The clean floor (1.19) lands in the **same regime as the sibling's smooth analytic
shapes** (sphere 1.20, cylinder 1.17) — the mixed scene's camera-yaw crawl on master is
clean. The capture is **byte-identical run-to-run** (fixed camera poses + frozen
entities + the deterministic re-voxelize bake), so 1.19 is a hard number, not a noisy
mean: the inherent round-to-cell scatter speckle is sub-cell and per-pose deterministic
here, so it does not lift the gate.

### Choosing the gate

`MAX_JITTER = 4.0` mirrors the sibling's gate — same metric, same ~1.2 clean floor — and
gives a >3× margin above the measured 1.19 while staying below where a real crawl
regression lands (the sibling's failing shapes hit 6–11 on the identical metric). With
no live FAIL sample in the mixed scene (the #1942 spatial drift is a different artifact
class, gated elsewhere), the gate's teeth are demonstrated by tightening it below the
floor — `MAX_JITTER=1.0 bash scripts/dev/canvas-stress-rotate-jitter-sweep` exits
non-zero — confirming it *would* fail on a crawl regression that pushes the score above
the floor. Re-baseline and tighten once a future re-voxelize / per-axis change moves the
clean number.

### OpenGL / Linux

As with the shape_debug baseline, this table is the **Metal** baseline (the authoring
host). The metric is backend-agnostic (luminance curvature), so the same `MAX_JITTER`
gate applies on OpenGL; the Linux baseline is captured by running the identical command
on a Linux host (cross-host smoke or a Linux worker) and is a follow-up — the renderer
behaviour, not the harness, is what differs between backends.

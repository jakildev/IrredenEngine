# One voxel receiver model across cardinal yaws: OpenGL readings

The OpenGL readings of #4965's criteria 1 to 3 for the receiver-model default,
taken on the arms #4965 names: head `aa3fd7ac8` (PR #5119's merge) and parent
`e230a0d2f` (its merge parent). The two arms differ only by PR #5119. The Metal
readings these compare with are in `docs/design/voxel-receiver-yaw-continuity.md`
§ "Correctness".

- **Host:** Windows 10 Pro 10.0.19045, MSYS2 mingw64, `windows-debug` (Debug).
- **GPU:** NVIDIA GeForce RTX 3060 Ti, driver 32.0.15.8180, OpenGL.
- **Configuration:** default on both arms (`IR_PERAXIS_SURFACE_LIGHTING` unset)
  unless a row names the switch.

Every figure below is in [results.json](results.json), with each graded frame's
SHA-256. The drivers are retained as text: [run.py](run.py.txt) (captures and
grades every criterion), [cross2x.py](cross2x.py.txt) (the crossing capture at
output scale 2), [lone.py](lone.py.txt) (the lone-frame count) and
[collect.py](collect.py.txt) (writes `results.json`). Their absolute scratch
paths describe the executed runs and must be replaced for a replay.

## Criterion 1: lone-frame ratio

`fleet-run IRShapeDebug --spin-yaw --zoom 16 --auto-screenshot 720`, then
`python3 scripts/render-yaw-lone-frame-metric.py <shots> --yaws 0,90,180,270
--max-ratio 1.3`. The frames are 1280×720.

| Yaw | Parent | Head |
|---|---:|---:|
| 0° | 1.433 (fail) | 1.154 |
| 90° | 1.068 | 0.998 |
| 180° | 1.153 | 0.986 |
| 270° | 1.311 (fail) | 1.053 |
| Exit | 1 (`"pass": false`) | 0 (`"pass": true`) |

The head passes every cardinal, and every parent ratio is higher than the
head's. The script prints its verdict as the JSON `pass` field and its exit
code; it has no `RESULT=` line.

## Criterion 2: receiver-polygon oracle at 2560×1440

`scripts/render_receiver_shadow_polygon.py` is fixed to 2560×1440. On this host
a 2560×1440 offscreen request writes 2560×1421 frames while #4852 is open. Both
arms therefore ran with that issue's window commit `f4ac60d93` applied on top:
`engine/window`, `engine/render/src/opengl/opengl_render_impl.cpp` and
`creations/demos/shape_debug/main.cpp`, with no lighting or shader file. With
it, every run logs `OpenGL offscreen screen target attached (2560x1440)` and
writes a 2560×1440 PNG (IHDR checked per frame). The capture is the
evidence-directory command
(`docs/evidence/receiver-shadow-polygon/README.md`) with an
`init_window_width=2560, init_window_height=1440` window over a 1280×720 game
resolution. That is the shape of Metal's 1280×720-point retina frame. Every run
is `RESULT=CLEAN`.

| Yaw, radians | Parent, unset | Head, unset | Head, `0` | Head, `1` |
|---|---:|---:|---:|---:|
| 0.6 | 18 / 638 | 0 / 0 | | |
| π/2+0.6 | 32 / 650 | 0 / 0 | | |
| π+0.6 | 18 / 638 | 0 / 0 | | |
| 3π/2+0.6 | 32 / 650 | 0 / 0 | | |
| 0, π/2, π, 3π/2 | | | 0 / 0 each | 0 / 0 each |

Counts are missing / excess pixels outside the one-pixel band. Every case has
0 unknown-color pixels and valid coverage. The parent's intermediate counts are
the Metal parent's to the pixel, which shows the overlay does not move the
fixture.

## Criterion 3: crossing shadow mask

`python3 scripts/render-yaw-crossing-metric.py capture --output <dir>
--cardinal-deg <c>`, 1280×720 frames, output scale 1. The pairs are
−0.5°/−0.25°, −0.25°/0°, 0°/+0.25° and +0.25°/+0.5° around the cardinal. A
lone pixel is one where the cardinal frame differs from both neighbours while
the neighbours agree.

| Cardinal | Parent pairs (px) | Parent | Head pairs (px) | Head | Lone px, parent / head |
|---|---|---|---|---|---:|
| 0° | 0 / 224 / 224 / 0 | FAIL | 8 / 6 / 4 / 4 | PASS | 224 / 0 |
| 90° | 0 / 222 / 222 / 0 | FAIL | 4 / 2 / 6 / 3 | **FAIL** | 222 / 0 |
| 180° | 0 / 224 / 224 / 0 | FAIL | 8 / 6 / 4 / 4 | PASS | 224 / 0 |
| 270° | 0 / 222 / 222 / 0 | FAIL | 4 / 2 / 6 / 3 | **FAIL** | 222 / 0 |

The head reads `RESULT=FAIL` at 90° and 270°, and that is not a discontinuity:

- **No model switch.** The head has no lone pixel at any cardinal. Each
  differing pixel is an isolated pixel on the cube's shadow silhouette, which
  is the edge moving a fraction of a pixel per step. The parent's per-face
  receiver shows 222 to 224 lone pixels at every cardinal.
- **The same on Metal.** At output scale 2 (the overlay, `cross2x.py`) the head
  reads 32 / 24 / 16 / 16 at 0°, PR #5119's Metal head reading to the pixel, and
  16 / 8 / 24 / 12 at 90°, still FAIL. Each figure is exactly 4× the 1x count.
  The 1x parent counts are exactly a quarter of Metal's 0 / 896 / 896 / 0.
  PR #5119 posts one reading per arm, at the script's default 0° cardinal, so
  Metal's 90° and 270° figures are untaken.
- **Not the #5119 default.** At 90°, head with the switch at `1`, parent with
  the switch at `1` and head unset write byte-identical frames. The continuous
  route read 4 / 2 / 6 / 3 there as an opt-in before PR #5119.

The metric passes a cardinal only when its larger step is no larger than the
larger rotated step, so an edge step of 6 against 4 fails it. The pass rule is
filed as #5184. The kept frames are in `crossing-0/` and `crossing-90/`, one
directory per arm, and
`python3 scripts/render-yaw-crossing-metric.py grade <dir> --cardinal-deg <c>`
replays each reading offline.

Criterion 3 is therefore not met at 90° and 270°, and #5138 stays open on
#5184. If #5184 changes only the pass rule, the 0° and 90° readings replay from
the kept frames. The 180° and 270° frames are not kept (their hashes are in
`results.json`; the 270° frames differ from the 90° ones), so those two
cardinals need a fresh capture on an OpenGL host.

## Criterion 4: unit tests

`fleet-run IrredenEngineTest --gtest_filter='PerAxisSurfaceLighting*'` on the
head's GL build: 4 tests, `[  PASSED  ] 4 tests`, `RESULT=CLEAN`.

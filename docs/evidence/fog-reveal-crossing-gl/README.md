# Fog-pass crossing continuity: OpenGL reading

This is the OpenGL reading of #5123's criterion 1, on the arms #5209 names:

- **Head:** `7ed1e271f` (PR #5202's merge).
- **Parent:** `6afc51320` (PR #5201), the merge parent.

The two arms differ only by PR #5202. The Metal readings these compare with
are in `docs/design/voxel-receiver-yaw-continuity.md` § "Stage 2 measurements
(fog-pass pipelines)".

- **Host:** Windows 10 Pro 10.0.19045, MSYS2 mingw64, `windows-debug` (Debug).
- **GPU:** NVIDIA GeForce RTX 3060 Ti, driver 32.0.15.8180, OpenGL.
- **Configuration:** no `IR_PERAXIS_*` variable set on any run. The driver
  removes any inherited one before it launches.

Every figure below is in [results.json](results.json), with each graded
frame's SHA-256, its PNG IHDR size, its logged `SURFACE-ROUTE` line, the
offscreen target line and the `ir-run` verdict. The drivers are kept as text.
[run.py](run.py.txt) runs one arm, and [collect.py](collect.py.txt) re-grades
every kept directory offline, asserts the result equals the one the capture
printed, and writes `results.json`. Their absolute scratch paths describe the
executed runs; replace them for a replay.

## How the arms were run

- **The script.** Every run uses `scripts/render-yaw-crossing-metric.py` from
  a `1bff5ea9d` tree. Its copy of the script is byte-identical to
  `7ed1e271f`'s: `git diff 7ed1e271f 1bff5ea9d` touches only
  `creations/demos/perf_grid/main.cpp`, `scripts/perf/`, and one line of
  `docs/agents/VALIDATION-RENDER-FEATURES.md`.
- **The head binary.** It was built from that `1bff5ea9d` tree. The same diff
  adds no file `IRCanvasStress` compiles, so the head binary is a build of
  `7ed1e271f`'s sources.
- **The parent binary.** It was built in its own `6afc51320` worktree. The
  script reached it through `IRREDEN_BUILD_DIR`.

Every launch went through `fleet-run`, and every run logged
`ir-run: RESULT=CLEAN`.

## Criterion: the fog-pass arm

`python3 scripts/render-yaw-crossing-metric.py capture --fog-reveal 16
--output <dir>`: the five captures, the script's 1280×720 window, output
scale 1. The pairs are −0.5°/−0.25°, −0.25°/0°, 0°/+0.25° and +0.25°/+0.5°.

| Arm | Pairs (px) | Result | Exit | Logged fog route, all five frames | Output scale |
|---|---|---|---:|---|---:|
| Head `7ed1e271f` | 8 / 6 / 4 / 4 | `RESULT=PASS` | 0 | `compose` | 1 |
| Parent `6afc51320` | 0 / 242 / 242 / 0 | `RESULT=FAIL` | 1 | `fallback` | 1 |

The head's rotated frames log `SURFACE-ROUTE cardinal=0 perAxis=1
fog=compose fogComposed=1`, and its cardinal frame logs `cardinal=1
perAxis=0`. Every parent frame logs `SURFACE-ROUTE cardinal=0 perAxis=0
fog=fallback fogComposed=0`. Coverage is valid on both arms. The receiver
region is 6,188 px, and the head's shadow covers 826 to 836 px of it per
frame. The parent's shadow covers 1,024 px on each rotated frame and 832 px
on the cardinal one. Its rotated steps are 0 px and both cardinal steps are
242 px, so the receiver model changes at the crossing.

## Criterion: the fog-free arm

The same command without `--fog-reveal`, on the head:

| Arm | Pairs (px) | Result | Exit | Logged fog route | Output scale |
|---|---|---|---:|---|---:|
| Head `7ed1e271f` | 8 / 6 / 4 / 4 | `RESULT=PASS` | 0 | `none` | 1 |

## At Metal's capture shape

The same three readings at output scale 2: a 2560×1440 window over the same
1280×720 game resolution, the shape of Metal's retina frame. The only change
is `run.py`'s one-line window swap in the script's `config.lua`; no source
file changes. Both arms contain PR #5186, so every frame logs `OpenGL
offscreen screen target attached (2560x1440)` and is 2560×1440 by IHDR. No
overlay is needed.

| Arm | GL pairs (px) | GL result | Metal pairs (PR #5202) | Metal result |
|---|---|---|---|---|
| Head, `--fog-reveal 16` | 32 / 24 / 16 / 16 | PASS, `compose` | 32 / 24 / 16 / 16 | PASS, `compose` |
| Parent, `--fog-reveal 16` | 0 / 968 / 968 / 0 | FAIL, `fallback` | 0 / 968 / 968 / 0 | FAIL, `fallback` |
| Head, no flag | 32 / 24 / 16 / 16 | PASS | 32 / 24 / 16 / 16 (stage 2 text) | PASS |

OpenGL reproduces every Metal figure to the pixel. Each 1x count is exactly a
quarter of its 2x count.

## The GLSL the runs compiled

`layout(depth_greater) out float gl_FragDepth` is unconditional in
`ir_peraxis_scatter_fragment_body.glsl`. That file is shared by every per-axis
scatter fragment program. The head runs loaded `PerAxisScatterProgram`,
`PerAxisSurfaceLightingProgram`, `PerAxisSurfaceLightingFogProgram`,
`PerAxisVisibleLightingProgram`, `PerAxisVisibleLightingFogProgram` and
`PerAxisSurfaceShadowProbeProgram`, and the rotated frames drew through the
fog variant. So the NVIDIA GL driver compiled and linked the depth bound and
`v_peraxis_scatter.glsl`'s early-depth vertex output. A failed compile or
link is fatal in `opengl_shader.cpp`. The engine does not log driver
warnings, so this reading does not show a warning-free compile.

## Kept frames

`frames-head/` and `frames-parent/` hold the five 1x fog-reveal frames of each
arm. `python3 scripts/render-yaw-crossing-metric.py grade <dir>` replays the
pair counts offline. It cannot replay the routes, because the run logs are
not kept; the routes are in `results.json`. The fog-free and 2x frames are
recorded by hash only.

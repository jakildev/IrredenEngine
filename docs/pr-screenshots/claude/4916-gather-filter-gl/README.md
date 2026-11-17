# Gather-filter diagnostic evidence, OpenGL

Readings behind the OpenGL rows of `docs/design/continuous-camera-zoom.md` § "Filtered
minification". Windows, NVIDIA, framebuffer 1280x720, output scale 1. The Metal set is
`docs/pr-screenshots/claude/gather-filter-diagnostic/` at media commit `37a3b3eb`.

- `results.json`: `python3 scripts/render-continuous-zoom-metric.py filter --output <dir>`,
  `RESULT=PASS`, exit 0. Same keys as the Metal set: per sweep the zoom, density and output
  scale, each arm's ownership counts against nearest, the GRID / revoxelized temporal
  differences and the interior mean-colour spread.
- `gpu-timing.txt`: five alternating 600-frame `--auto-profile` runs per arm, twice. OpenGL
  has no whole-frame GPU timer (`Coverage: supported=0`), so the first table is the steady
  wall-frame time with GPU stage timing off. The second repeats the runs with stage timing on
  and adds the exclusive GPU time of the gather stages (`trixelToFb`, `entityCanvasToFb`) and
  of `fbToScreen` as an untouched control. `quiet` is ir-run's host-quiet verdict.

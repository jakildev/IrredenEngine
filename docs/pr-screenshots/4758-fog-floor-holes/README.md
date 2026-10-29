# FogDemo high-ground floor holes: classification evidence

Native macOS / Metal, base `469402034`. Every capture uses `config.lua` (1280x720
game resolution, `--auto-screenshot 10`, settle 3) through `fleet-run IRFogDemo`.
The four head captures are byte-identical (`cmp`) to the same-session master
captures, so the probe change does not move the rendered frame.

| file | argv |
|---|---|
| `treated_soft_los.png` | `--occlusion=high-ground --los-softness=1 --reveal-treatment` |
| `untreated_soft_los.png` | `--occlusion=high-ground --los-softness=1` |
| `untreated_hard_los.png` | `--occlusion=high-ground` |
| `fully_revealed_flat.png` | `--occlusion=flat` |
| `hole_overlay.png` | the treated capture with every hole pixel painted red |

## Verdict: intended reveal-surface dissolve

The holes are the treatment's per-voxel dissolve of the soft line-of-sight band.
They are not a line-of-sight, face-admission, depth-winner or composition defect.

- **Treatment on/off.** `hole_mask.py` (output in `hole_mask.txt`) defines a hole
  pixel as one that is black in the treated capture and not black in the
  untreated soft capture at the same pose. It finds 35552 hole pixels in 44
  components, every one a whole voxel diamond inside the soft band. None of
  them is black in the hard-LOS control or in the fully revealed control.
- **In-run oracle.** The `FOG-HOLE-ORACLE` lines in `treated_run.log` grade the
  capture frame itself at subdivision 8. They classify every slab-top texel
  inside radius 11 using only the authored ridge box (snapped to the raster
  lattice: x -0.5..1.5, y -7.5..8.5, top plane -0.5), the eye, the softness, and
  the dissolve hash and density. Of the 27349 graded texels, the authored policy
  misses 0, and the 3636 key-seam texels each match one of their adjacent keys.
  All 3759 treated holes lie inside the authored dissolve. The treatment-off
  control frame (the settle frame ahead of the capture) removes no texel with
  visibility >= 0.1. The lit composite under every hole is the slab's own: the
  matter, depth winner and lighting are intact until the fog pass paints the
  texel unexplored at state 0.
- **Corrupted decisions are rejected.** Of the 13514 graded band texels, the four
  corrupted CPU policies miss: LOS ignored 11483, hard LOS 3697, admission
  inverted 13514, dissolve keyed on the +X neighbour 3886.

## Positive controls (shader mutations, Metal)

Each control mutates one decision in the Metal shader, rebuilds, and re-runs the
treated capture. `mutations/*.log` holds each run's probe lines. These runs set a
temporary environment switch that skipped the warmup `FOG-SURFACE-PROBE`
requires, so the capture-pose oracle was reached. That switch is not in the
committed code.

| mutation | `ir_fog_*.metal` edit | authored misses |
|---|---|---|
| `admission` | `hash01 > mix(…)` → `hash01 <= mix(…)` | 13363 (FAIL) |
| `losclearance` | `smoothstep(0, softness, bandClearance)` → `… bandClearance + 0.25` | 2066 (FAIL) |
| `keyshift` | `fogRevealSurfaceHash01(surfaceVoxel)` → `(surfaceVoxel + int3(1, 0, 0))` | 3822 (FAIL) |

## Fixture note

The warmup frames render at subdivision 1, where the half-integer ridge set
snaps to x 0..2 with its top plane at 0. At that pose the observer at x = 0
stands on the ridge's -X edge, so only the +X ground has a shadow band. The
shot renders at subdivision 8, where the ridge keeps its authored half-integer
placement and both sides show a band. The warmup `FOG-SURFACE-PROBE` therefore
grades a different geometry from the screenshot. The hole oracle grades the
capture frame.

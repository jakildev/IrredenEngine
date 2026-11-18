# Main-canvas sun-shadow dispatch skip

This record grades the main-canvas `COMPUTE_SUN_SHADOW` skip against parent
`18a91f694` (master). On a cardinal surface-lit frame, the skip fires when
every occupied main-canvas texel is a cardinal voxel surface. The head is that
parent plus this change.

## Method

The parent and the head were built as separate Debug trees and run on Metal
(Apple M4 Max, AC power). The pairs were interleaved, parent first in odd
rounds and head first in even rounds. Each run took its own guarded benchmark
lease:

```text
IR_PERAXIS_SURFACE_LIGHTING=1 IR_PERAXIS_VISIBILITY_STATS=0 \
python3 scripts/perf/repeat_profile.py --target IRCanvasStress \
  --build-dir <arm build> --output <pair-arm> --repeats 1 --timeout 120 -- \
  <scene_arguments("shadowbox")> --auto-profile --profile-frames 360 \
  --window-mode offscreen <cases("shadowbox")["cardinal-zoom4-overlap8-subdiv4"]>
```

The scene arguments and cases come from `scripts/perf/surface_lighting_controls.py`.

`comparison.json` holds every run, `base_runs` for the parent and `head_runs`
for the head, paired by round.

- Seven rounds ran. Round 3's head recorded a foreign-dispatch
  `QUIET-BREACH` (`RESULT=CONTAMINATED`), so that whole pair is dropped. The
  other six pairs record `quiet=guarded` and `RESULT=CLEAN`.
- Parent binary SHA-256: `a8bbff504ec008b3…`. Head binary SHA-256:
  `e19b0cb46a0d…`. Full values are in `comparison.json`.
- Every head run logs `COMPUTE_SUN_SHADOW: main-canvas dispatch skipped`, and
  no parent run does.
- The head's `computeSunShadow` GPU row is absent, because the stage encodes
  no GPU work in the window. Grading reads that row as 0 ms. On this fixture
  the probe's entity canvases carry no `C_CanvasSunShadow`, so the main canvas
  is the row's only contributor.

## Result

| Row | Parent mean (ms) | Head mean (ms) | Paired delta (ms) | One-sided 95% upper bound |
|---|---:|---:|---:|---:|
| `computeSunShadow` | 0.308 | 0 (absent) | −0.308 (sd 0.016) | −0.295 ms = −95.7% of parent |
| frame avg | 11.158 | 11.048 | −0.110 (sd 0.776) | ratio 0.990 |
| GPU `commandBufferSpans` | 7.094 | 7.130 | +0.037 (sd 0.215) | ratio 1.005 |

- The criterion bar is an upper bound at or below −50% of the parent mean, and
  the stage row clears it.
- Both frame rows pass rule (i) of
  `docs/design/gpu-stage-timing-cost-model.md` §3 with ratio limit 1.05.
- The GPU frame cannot resolve a 0.3 ms change at this spread; its paired sd
  is 0.215 ms.

## Captures

`captures.json` records same-session parent/head comparisons with
`scripts/render-compare.py --per-pixel-tol 0`. All sets were captured on the
same parent and head trees.

- **Skip fires.** The criterion-1 fixture at subdivisions 1 and 4, and a
  nine-yaw sweep of it.
- **Cardinal ↔ rotating transitions.** The nine-yaw
  `--sweep-yaw 0 6.2831853 9` `IRCanvasStress` set, with the default SDF floor
  and with `--probe-floor-mode grid`.
- **Dispatch runs.** The `IRShapeDebug` shot table, which has shapes on the main
  canvas, and its `--debug-overlay shadow` run.
- **Render-verify runs.** Every `IRPerfGrid` and `IRFogDemo` render-verify run
  (default pass plus each `extra_runs` entry, including `--ceiling-shadow`),
  replayed with the manifest's arguments and warmup.

Every pair has maximum delta 0. On the grid-floor sweeps the head logs the
skip on the five cardinal poses and `resumed` on the four rotating ones. No
run logs an assertion.

The host had an external 1x display attached, so every full frame here is
1280×720 on both arms. The committed macOS references are 2560×1440. A
reference-based `render-verify.py` pass on this host therefore fails on
shape alone, on parent and head alike. The head-against-parent diff stands in
for "no failure absent on the parent".

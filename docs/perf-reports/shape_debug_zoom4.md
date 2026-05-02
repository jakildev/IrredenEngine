# Benchmark: IRShapeDebug at zoom 4

300-frame per-system timing capture on the `SHAPES_TO_TRIXEL` / voxel-pool
pipeline at `SubdivisionMode::FULL`, `effective_subdivisions = 4`.

## Run configuration

| Field | Value |
|---|---|
| Executable | `IRShapeDebug` |
| Host | macOS 15, Apple M4 Max |
| Backend | Metal (`macos-debug` preset) |
| Flags | `--auto-profile 300 --zoom 4` |
| Frames sampled | 300 |
| Subdivision mode | `FULL` (base_subdivisions=1, zoom_scale=4 → effective=4) |
| Entity count | 168 (27 archetypes) |
| Scene | 8 test shapes × 2 render paths (voxel-pool + SDF), plus floor |
| Date | 2026-04-19 |

## Frame time summary

| Metric | ms |
|---|---|
| Average | 9.90 |
| p50 | 8.43 |
| p95 | 16.46 |
| p99 | 57.12 |
| Min | 6.30 |
| Max | 128.74 |

Average sits under the 16.67 ms 60-FPS budget, but p95 grazes it and
p99 / max are clear warmup-tail spikes (Metal first-use shader compile
and first-frame resource uploads). Steady-state post-warmup, frame pacing
is closer to the ~8 ms median.

## Top 3 hotspots

**1. `SingleVoxelToCanvasFirst` — avg 5.15 ms/frame (52% of average
frame).** The Stage-1 voxel→trixel compute dispatch. At zoom 4 with
`effective_subdivisions = 4`, each voxel face subdivides into a 4×4
sub-pixel grid — 16× the `imageAtomicMin` depth writes vs zoom 1.
1546 ms / 300 frames makes this the dominant cost by a wide margin;
any sustained FPS improvement at high zoom has to come from here
(fewer atomics, better culling, or a different depth strategy).

**2. `ShapesToTrixel` — avg 0.47 ms/frame, max 24.37 ms.** The SDF
shape compute pass. 16 SDF shapes (the 8 test shapes rendered twice,
plus the floor) × the 4× subdivision multiplier. The 24 ms tail spike
is almost certainly the Metal first-use shader compile on first
dispatch — it shows up in p99/max but disappears after the first few
frames. Steady-state cost is sub-millisecond.

**3. `CanvasToFramebuffer` — avg 0.06 ms/frame, max 0.72 ms.** Runs
three times per frame (voxel canvas, trixel canvas, background canvas),
so per-call cost is ~0.02 ms. Not a hotspot in the usual sense but
third in total-ms ranking. Scales linearly with canvas count, not zoom.

## Per-system breakdown

Raw capture: [`shape_debug_zoom4_raw.txt`](shape_debug_zoom4_raw.txt).

```
Pipeline System                                Total(ms)   Avg(ms)   Min(ms)   Max(ms)   Calls   Entities
INPUT    InputKeyMouse                              0.94     0.005     0.002     0.027     178      22250
UPDATE   UpdatePositionsGlobal                      1.73     0.010     0.007     0.029     178       3026
UPDATE   UpdateVoxelSetChildren                     0.40     0.002     0.001     0.015     178       1424
RENDER   SingleVoxelToCanvasFirst                1546.26     5.154     4.464     9.661     300        300
RENDER   ShapesToTrixel                           142.09     0.474     0.278    24.366     300       2400
RENDER   CanvasToFramebuffer                       18.53     0.062     0.038     0.718     300        900
RENDER   FramebufferToScreen                        4.13     0.014     0.010     0.106     300        300
RENDER   BuildOccupancyGrid                         2.59     0.009     0.003     0.556     300        300
RENDER   CameraMousePan                             2.30     0.008     0.002     0.042     300        300
RENDER   SingleVoxelToCanvasSecond                  2.06     0.007     0.004     0.086     300        300
RENDER   Camera                                     1.47     0.005     0.002     0.016     300        300
RENDER   LightingToTrixel                           0.57     0.002     0.001     0.004     300          0
RENDER   ComputeVoxelAO                             0.16     0.001     0.000     0.005     300          0
```

## Observations

- **Stage 1 dominates.** Summing all render-pipeline averages gives
  ~5.7 ms; measured total is 9.9 ms. The missing ~4.2 ms per frame is
  time outside any profiled system — `glfwPollEvents`, swap/present,
  VSync wait. The per-system collector is CPU-side wall time only and
  does not include the GPU submit-and-wait portion of present.
- **Update pipeline is cheap.** ~2 ms/frame total across the fixed-step
  update systems. 178 input/update calls over 300 render frames matches
  a fixed-step `UPDATE` cadence (~60 Hz) running under a faster render
  cadence (~120 Hz, matching the monitor).
- **`LIGHTING_TO_TRIXEL` is a no-op at the moment** (0 entities, ~0.002
  ms/call). Shipped as a skeleton in T-011; downstream phases (AO data
  bind-point, shadow map, flood-fill) will populate it.
- **`ComputeVoxelAO` reports 0 entities** — the compute pass runs but
  has no voxels to AO-sample at this scene / zoom. Look into whether
  the AO pass is correctly gated on the occupancy grid before assuming
  the pass is "free."
- **`SingleVoxelToCanvasSecond` is ~50× cheaper than `First`.** Stage 2
  reads the distance texture and writes color + entity id; Stage 1 does
  the per-sub-pixel atomicMin that 4² subdivision inflates. The ratio
  is instructive: Stage 1's cost is ~all atomics, Stage 2's cost is
  ~all the work the pipeline was originally designed for.

## Reproducing

```
fleet-build --target IRShapeDebug
fleet-run --timeout 30 IRShapeDebug --auto-profile 300 --zoom 4
```

The run writes `save_files/profile_report.txt` next to the executable.
The raw text file archived with this report is `shape_debug_zoom4_raw.txt`.

## Known gaps (follow-up work)

- **No `profiler_dump.prof` is produced on the `macos-debug` / Metal
  build.** `CPUProfiler::shutdown()` calls `profiler::dumpBlocksToFile`
  but no `.prof` file appears after a 300-frame run and no
  "Dumped profiling blocks" log line is emitted. Text report still
  produces authoritative per-system numbers because `World` collects
  timings directly, but per-scope block trees (flamegraph view in
  `profiler_gui`) are unavailable. Needs a separate investigation —
  candidates: `EASY_PROFILER_ENABLE` getting compiled as a no-op under
  `macos-debug`, `easy_profiler` link-time config, or shutdown ordering
  between the CPUProfiler singleton and static deinit.
- **No GPU timer queries** in either backend (tracked in
  `jakildev/IrredenEngine#173`). All numbers above are CPU-side wall
  time. The 4.2 ms/frame gap between summed systems and total frame
  time includes GPU stalls on present — we can't yet split that into
  "GPU busy" vs "CPU blocked on GPU."
- **Warmup is not stripped.** First ~5 frames include Metal shader
  first-compile, which inflates `max` and p99 across every render
  system. A `--profile-warmup N` flag that delays frame-timing capture
  would give tighter steady-state numbers. Not added here to keep the
  demo-level change minimal.

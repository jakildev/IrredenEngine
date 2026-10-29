# Explicit rendering capacity envelope

The checked-in matrix is [`scripts/perf/capacity_envelope.json`](../../../scripts/perf/capacity_envelope.json), executed by `capacity_envelope.py`. It keeps Metal and Windows/OpenGL in separate output roots and refuses a run without platform, backend, hardware, and power/thermal metadata. Every timed suite uses a Release tree, three guarded repetitions and 300 frames per cell; the multi-case drivers reverse case order on alternating rounds. The runner verifies and fingerprints the target's 1280×720 game-resolution fixture before launch. Results retain fresh complete frame series, GPU-frame coverage, CPU/system attribution, and explicit population, visibility, canvas, subdivision, shadow, and overflow budgets.

## Current qualification verdict

The current Metal one-million-entity cells do **not** meet 16.67 ms. No one-million capacity is established at any tested camera pose. The profiling-off continuous-rotation cell has a 62.85 ms pooled steady mean and 81.18 ms p99. This is a measured shortfall, not a reason to reduce required lighting or omit visible work.

Windows/OpenGL has no run of this matrix, so its capacity is **unqualified**. Existing Windows perf-grid baselines are smaller workloads and are not substituted for a million-entity result.

## Metal Release result

Hardware: Apple M4 Max MacBook Pro (14 CPU cores, 32 GPU cores, 36 GB), Metal, 1280×720. The run used AC power, but battery charge moved from 81% to 77%; the result is therefore provisional for power stability. One-minute load readings were 1.8–3.3, and `pmset -g therm` reported no recorded thermal or performance warning. The Release binary fingerprint was `f41adef275b24fdd`; shaders were `70da9f852c7e7129`. All 18 runs were guarded. Each pooled steady distribution contains 675 frames after excluding the first 75 frames of each run.

Command:

```bash
python3 scripts/perf/million_controls.py --tree build-release --rounds 3 --frames 300 --output <new-directory>
```

| Timing | Camera | Run means ms | Steady mean / p95 / p99 ms | GPU envelope ms | Updates/frame | Visible / 1,000,000 | Axis entries | Overflow entries / dropped | 60 Hz |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| off | cardinal | 49.94 / 49.50 / 49.58 | 48.23 / 49.72 / 52.62 | 18.60 | 2.9 | not read back | 0 | 0 / 0 | fail |
| off | 45° | 59.82 / 60.28 / 58.71 | 57.50 / 61.12 / 67.94 | 25.54 | 3.5 | not read back | not read back | 2,652,369 / 0 | fail |
| off | rotating 358.8° | 63.07 / 62.79 / 66.27 | 62.85 / 78.60 / 81.18 | 29.43 | 3.8 | not read back | not read back | 2,662,119 / 0 | fail |
| on | cardinal | 50.97 / 49.96 / 50.06 | 49.00 / 50.17 / 63.51 | 18.77 | 2.9 | 786,156 (78.62%) | 0 | 0 / 0 | fail |
| on | 45° | 60.15 / 64.51 / 60.02 | 59.47 / 68.57 / 76.69 | 26.36 | 3.6 | 909,358 (90.94%) | 2,728,074 | 2,652,369 / 0 | fail |
| on | rotating 358.8° | 64.52 / 63.02 / 69.86 | 64.95 / 84.78 / 101.48 | 30.39 | 3.9 | 906,551 avg (90.66%) | 2,688,040 avg | 2,662,119 / 0 | fail |

Profiling-off frame-mean run ranges are 49.50–49.94 ms cardinal, 58.71–60.28 ms at 45°, and 62.79–66.27 ms while rotating. The one-sided 95% upper bounds on those three-run means are 50.07, 60.96, and 67.30 ms. The timing-on/off pairs agree on the shortfall; stage instrumentation is not its cause. Visibility readback is intentionally confined to timing-on cells, so the off rows are not presented as zero visible work.

## Metal Release motion/subdivision result

Each cell below has three guarded 300-frame runs and 675 pooled steady frames. Values are steady-frame mean followed by the one-sided 95% upper bound in parentheses. Every cell is above the 16.67 ms budget except the wave-0, zoom-1, base-1 cardinal mean; its upper bound still exceeds budget. Raising base subdivisions from 1 to 4 at wave 0 and zoom 1 raises cardinal steady mean from 16.39 ms to 22.47 ms, positively exercising changed subdivision work in Release.

| Wave | Zoom / base subdivisions | Cardinal ms | Diagonal ms | Continuous sweep ms |
|---:|---:|---:|---:|---:|
| 0 | 1 / 1 | 16.393 (17.893) | 21.160 (21.492) | 21.030 (22.079) |
| 0 | 1 / 4 | 22.473 (22.586) | 23.110 (24.375) | 22.153 (24.153) |
| 0 | 4 / 4 | 24.857 (25.272) | 23.037 (23.327) | 22.743 (23.508) |
| 5 | 1 / 1 | 20.423 (20.583) | 24.383 (24.671) | 25.953 (26.670) |
| 5 | 1 / 4 | 23.690 (24.290) | 26.680 (26.961) | 28.493 (28.949) |
| 5 | 4 / 4 | 93.770 (94.909) | 26.177 (26.593) | 28.770 (29.015) |

The wave-5, zoom-4, base-4 cardinal case is GPU-bound: its 74.47 ms GPU envelope is dominated by 71.35 ms `computeLightVolume`. The matching sweep has an 11.88 ms GPU envelope led by 6.10 ms light-volume work, 3.56 ms per-axis compaction, and 2.81 ms scatter; its 28.77 ms wall mean shows CPU/system work remains material.

## Metal Release surface/prepass result

The four arms compare sampled (`surface=0`) and continuous (`surface=1`) receiver work with the visibility prepass off/on. Values are steady-frame mean followed by the one-sided 95% upper bound in parentheses, again over three guarded runs and 675 pooled steady frames per arm.

| Zoom / subdivisions | Sampled, prepass off ms | Sampled, prepass on ms | Continuous, prepass off ms | Continuous, prepass on ms |
|---:|---:|---:|---:|---:|
| 1 / 1 | 9.493 (11.205) | 10.910 (12.003) | 13.757 (19.504) | 17.920 (19.395) |
| 1 / 4 | 13.177 (19.211) | 14.900 (16.114) | 20.523 (21.436) | 21.000 (21.971) |
| 4 / 1 | 9.120 (11.165) | 9.413 (12.431) | 17.997 (19.946) | 16.137 (19.839) |
| 4 / 4 | 15.447 (19.618) | 17.217 (18.428) | 16.930 (18.980) | 18.510 (21.794) |

Continuous receiver draw is the first added bottleneck: at zoom 4/base 4 with prepass on, `perAxisSurfaceFb` averages 2.65 ms inside a 6.10 ms GPU envelope, versus 1.90 ms for `trixelToFb`. Prepass does not establish a throughput win in this matrix; its positive rejection remains witnessed by the separate untimed counters below.

## Metal Release private-canvas result

The eight-private-canvas orbit misses 60 Hz: steady mean is 21.853 ms, the one-sided 95% upper bound is 22.606 ms, and pooled steady p95/p99 are 25.149/26.635 ms. Its GPU envelope is only 7.509 ms, so CPU/system and presentation work dominate this budget rather than a single measured GPU stage.

## Bottleneck ranking

GPU frame envelopes alone exceed 16.67 ms in every pose. Sampled GPU scopes overlap and are not additive, but they identify the largest work:

1. Cardinal `computeLightVolume` averages 16.99 ms; cardinal voxel stage 1 and stage 2 average 5.17 and 4.44 ms.
2. At 45°, `computeLightVolume`, per-axis cell compaction, and per-axis AO each average about 15 ms; overflow production averages 12.07 ms, overflow lighting 7.42 ms, and scatter 5.94 ms.
3. Continuous rotation raises scatter to 8.38 ms and preserves roughly 12.50 ms overflow production, while pooled wall-frame p99 reaches 101.48 ms with timing enabled and 81.18 ms without it.
4. CPU fixed updates compound the miss: each update traverses one million entities in `UpdateVoxelSetChildren` and `FogRevealEval` at roughly 1.5–1.6 ms apiece, plus transform propagation and modifier decay. Slow frames therefore execute 2.9–3.9 updates, while `SingleVoxelToCanvasFirst` costs about 4.6–4.9 ms per rendered frame.

The dominant measured constraint is combined GPU volume/per-axis work, with million-entity CPU update catch-up widening wall time. The result does not select an implementation optimization; each candidate needs an isolated base/head control.

## Correctness oracles

The private-canvas orbit has eight private canvases spanning 120–192 pixels per edge and 209,520 private-canvas pixels total, separate from the 1280×720 main canvas. The timed suites retain throughput evidence separately from these untimed correctness controls:

- The overlapping GRID prepass controls reject 4,094 of 17,684 candidates at zoom 1 and 64,757 of 282,102 at zoom 4 (about 23%), while prepass-off emits no counter row and every paired decoded RGBA image is identical.
- The receiver-polygon controls preserve sharp geometry-based shadow coverage independently of the timing matrix.
- Crowded overflow image controls remain separate from the timed overflow counts; zero dropped entries in the million run does not by itself prove shadow completeness.

The remaining qualification step is the independent Windows/OpenGL ship-host run. The Metal power-stability caveat also requires a quiet repeat with stable charge before any default promotion.

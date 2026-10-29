# Explicit rendering capacity envelope

The checked-in matrix is [`scripts/perf/capacity_envelope.json`](../../../scripts/perf/capacity_envelope.json), executed by `capacity_envelope.py`. It keeps Metal and Windows/OpenGL in separate output roots and refuses a run without platform, backend, hardware, and power/thermal metadata. Every timed suite uses a Release tree, three guarded repetitions and 300 frames per cell; the multi-case drivers reverse case order on alternating rounds. The runner verifies and fingerprints the target's 1280×720 game-resolution fixture before launch. Results retain fresh complete frame series, GPU-frame coverage, CPU/system attribution, and explicit population, visibility, canvas, subdivision, shadow, and overflow budgets.

## Current qualification verdict

The current Metal one-million-entity cells do **not** meet 16.67 ms. No one-million capacity is established at any tested camera pose. The profiling-off continuous-rotation cell has a 62.85 ms pooled steady mean and 81.18 ms p99. This is a measured shortfall, not a reason to reduce required lighting or omit visible work.

Windows/OpenGL has no run of this matrix, so its capacity is **unqualified**. Existing Windows perf-grid baselines are smaller workloads and are not substituted for a million-entity result.

## Metal Release result

Hardware: Apple M4 Max MacBook Pro (14 CPU cores, 32 GPU cores, 36 GB), Metal, 1280×720. The run used AC power; battery charge moved from 81% to 77%, one-minute load readings were 1.8–3.3, and `pmset -g therm` reported no recorded thermal or performance warning. The Release binary fingerprint was `f41adef275b24fdd`; shaders were `70da9f852c7e7129`. All 18 runs were guarded. Each pooled steady distribution contains 675 frames after excluding the first 75 frames of each run.

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

## Bottleneck ranking

GPU frame envelopes alone exceed 16.67 ms in every pose. Sampled GPU scopes overlap and are not additive, but they identify the largest work:

1. Cardinal `computeLightVolume` averages 16.99 ms; cardinal voxel stage 1 and stage 2 average 5.17 and 4.44 ms.
2. At 45°, `computeLightVolume`, per-axis cell compaction, and per-axis AO each average about 15 ms; overflow production averages 12.07 ms, overflow lighting 7.42 ms, and scatter 5.94 ms.
3. Continuous rotation raises scatter to 8.38 ms and preserves roughly 12.50 ms overflow production, while pooled wall-frame p99 reaches 101.48 ms with timing enabled and 81.18 ms without it.
4. CPU fixed updates compound the miss: each update traverses one million entities in `UpdateVoxelSetChildren` and `FogRevealEval` at roughly 1.5–1.6 ms apiece, plus transform propagation and modifier decay. Slow frames therefore execute 2.9–3.9 updates, while `SingleVoxelToCanvasFirst` costs about 4.6–4.9 ms per rendered frame.

The dominant measured constraint is combined GPU volume/per-axis work, with million-entity CPU update catch-up widening wall time. The result does not select an implementation optimization; each candidate needs an isolated base/head control.

## Other declared budgets and oracles

The matrix also declares Release motion/subdivision, sampled/continuous receiver with prepass off/on, and an eight-private-canvas orbit fixture. The orbit case has eight private canvases spanning 120–192 pixels per edge and 209,520 private-canvas pixels total, separate from the 1280×720 main canvas. Those suites remain unexecuted on the current head, so they establish no additional Release capacity.

Existing untimed controls remain valid correctness evidence rather than being folded into throughput:

- The overlapping GRID prepass controls reject 4,094 of 17,684 candidates at zoom 1 and 64,757 of 282,102 at zoom 4 (about 23%), while prepass-off emits no counter row and every paired decoded RGBA image is identical.
- The receiver-polygon controls preserve sharp geometry-based shadow coverage independently of the timing matrix.
- Crowded overflow image controls remain separate from the timed overflow counts; zero dropped entries in the million run does not by itself prove shadow completeness.

The retained Debug motion matrix positively exercises changed subdivision work and reports dense cardinal zoom-4/base-4 at 88.733 ms steady mean, but it is not promoted to a Release capacity claim. A complete qualification next runs every declared suite on a quiet Metal host and then repeats the same specification independently on the Windows/OpenGL ship host.

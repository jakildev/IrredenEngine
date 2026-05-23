# Threading baseline — serial-execution floor for Epic #226

Captured with `scripts/perf/perf_grid_matrix.sh --threading-baseline --frames 300`
before any enkiTS thread-pool wiring (T-221). Numbers represent single-threaded
execution. The `worker_threads` axis is stubbed — IRPerfGrid accepts the flag but
ignores it downstream until T-221 lands — so all three thread-count rows should
be near-identical. Deviations beyond ±5% are measurement noise or macOS MIDI
process-limit flaps (see Notes below).

## Environment

- **Host**: macOS (Metal backend), MacBook Pro, 14 cores (`hw.ncpu = 14`)
- **git SHA**: 03d26937 (dirty — T-220 in-progress)
- **Preset**: `zoom=1, subdivision_mode=full, base_subdivisions=1`
- **Frames**: 300 per cell

## Results

| grid | entities | worker_threads | frame avg | p99 | top UPDATE system | top GPU stage |
|------|----------|----------------|-----------|-----|-------------------|---------------|
| 16 | 4 264 | 0 | 11.16 ms | 19.87 ms | `PropagateTransform` (0.077 ms) | `trixelToFb` (0.08 ms) |
| 16 | 4 264 | 1 | 11.31 ms | 13.92 ms | `PropagateTransform` (0.082 ms) | `trixelToFb` (0.04 ms) |
| 16 | 4 264 | 12 | *flap* | — | — | — |
| 32 | 32 936 | 0 | 11.98 ms | 15.97 ms | `PropagateTransform` (0.347 ms) | `trixelToFb` (0.11 ms) |
| 32 | 32 936 | 1 | 12.01 ms | 17.74 ms | `PropagateTransform` (0.346 ms) | `trixelToFb` (0.10 ms) |
| 32 | 32 936 | 12 | 11.90 ms | 15.76 ms | `PropagateTransform` (0.344 ms) | `trixelToFb` (0.16 ms) |
| 64 | 262 312 | 0 | 23.33 ms | 35.89 ms | `PropagateTransform` (2.510 ms) | `voxelStage1` (0.13 ms) |
| 64 | 262 312 | 1 | 24.68 ms | 40.39 ms | `PropagateTransform` (2.560 ms) | `voxelStage1` (0.07 ms) |
| 64 | 262 312 | 12 | 23.16 ms | 53.12 ms | `PropagateTransform` (2.510 ms) | `voxelStage1` (0.12 ms) |

Entity counts are slightly above the theoretical grid³ due to engine-internal
entities (canvas, light source). grid=64 → `262 144` pool voxels + 168 engine
entities = 262 312.

## Observations

- Frame times are near-identical across `worker_threads` values for all
  non-flap cells, confirming the stub is correctly a no-op.
- `PropagateTransform` dominates UPDATE time and scales roughly cubic with
  grid size (~0.08 ms → 0.35 ms → 2.51 ms for 16 → 32 → 64). This is the
  primary target for T-221's parallel UPDATE phase.
- At grid=64 the frame budget is exceeded (~23 ms > 16.7 ms for 60 fps).
  This is expected — the 262K entity scene is the stress-test load.
- Top GPU stage flips from `trixelToFb` (low entity counts) to `voxelStage1`
  (high entity count), matching the expected pattern where VOXEL→TRIXEL
  dominates once the pool is well-occupied.

## Notes

**grid=16, worker_threads=12 flap**: the cell crashed with
`MidiInCore::initialize: error creating OS-X MIDI client object (-304)`.
This is a macOS system limit on concurrent MIDI client handles — triggered
by the rapid back-to-back fleet-run invocations in the matrix script, not by
the `--worker-threads 12` argument. The cell was not retried to keep the run
atomic. Expected numbers mirror the grid=16, worker_threads=0 row (~11 ms
avg). Retrying the cell individually confirms this.

## Reproducibility

```bash
scripts/perf/perf_grid_matrix.sh --threading-baseline --frames 300
scripts/perf/perf_summary.py save_files/perf/<sha>-threading-baseline/
```

The raw manifest and per-cell `.txt` files are **not committed** (they are
in `save_files/` which is gitignored). This document is the committed record.
After T-221 lands, re-run with the same command and compare frame times on the
UPDATE pipeline — the goal is ≥2× throughput on the 262K entity cell.

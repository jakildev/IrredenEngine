# perf profiling recipe

The canonical before/after ritual, matrix size knobs, demo CLI flags, config
presets, cull stats, Lua-vs-C++ parity, committed baselines, and the CI gate
are documented once in [`docs/perf/README.md`](../../../../docs/perf/README.md).
Link that file from PR bodies; do not paraphrase the flags.

## Baseline from a fleet worktree

`git checkout master` is unsafe in a worktree (another checkout holds
`master`). Use a throwaway worktree:

```bash
git worktree add /tmp/perf-baseline master
(cd /tmp/perf-baseline && scripts/perf/perf_grid_matrix.sh --label baseline)
git worktree remove /tmp/perf-baseline
```

or run the matrix in a sibling clean clone and pass its `save_files/perf/`
directory by absolute path to `compare_perf_runs.py`.

## Output layout

- `save_files/perf/<sha>[-<label>]/<cell-id>.txt` — per-cell `profile_report.txt`
- `save_files/perf/<sha>[-<label>]/<cell-id>.log` — per-cell stdout/stderr
- `save_files/perf/<sha>[-<label>]/manifest.json` — run metadata

`save_files/` is gitignored. `compare_perf_runs.py` reports voxel cull
effectiveness, frame timing (avg + p99 per cell with delta), GPU stage timing,
and top CPU systems per cell; regressions ≥ 10% flag `⚠`, improvements ≥ 5%
flag `↓` (`--regress-pct N --improve-pct M`).

## Reading a delta

- A per-cell delta is signal only when it clears that cell's run-to-run
  spread on an unchanged binary: re-run head 2–3× on the cell first. `p95` /
  `p99` are tail-dominated — weigh `p50` / avg.
- The matrix has no yaw axis (every cell runs at yaw 0), so a path gated off
  at cardinal (the per-axis scatter) reports 0% over code that never ran.
  Drive it by hand — `fleet-run IRPerfGrid --yaw 0.785 --auto-profile 400` —
  with ≥ 3 samples per arm.
- A cell that exceeds the per-cell `--timeout` (default 90 s) at extreme
  zoom/subdivision: `--frames 60`, `--timeout 180`, or narrow the matrix
  arrays in the script. A cell that consistently reports
  `status: no_report` in the manifest is itself a finding — record it in the
  PR body.

## The same data at runtime

- Lua: `ir.render.getPassTimings()`, `ir.render.getVoxelCullStats()`
  ([`gpu_profiling.md`](gpu_profiling.md)).
- HUD: `system_perf_stats_overlay.hpp` renders both live — the tool for "is
  the bug visible right now"; the matrix answers "did it get worse with this
  change".
- `save_files/profile_report.txt` — written at shutdown by every creation that
  called `IREngine::enableFrameTiming(true)`.

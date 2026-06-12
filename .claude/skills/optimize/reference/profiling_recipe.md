# perf profiling recipe

The full before/after measurement loop, written as the script commands
to invoke. Do not paraphrase these in PR bodies — link this file
instead.

## The canonical ritual

```bash
# 1. Capture baseline on master before the change.
git checkout master
git pull
scripts/perf/perf_grid_matrix.sh --label baseline

# 2. Switch to your feature branch (or apply the dirty tree) and run
#    the same matrix.
git checkout claude/my-optimization
scripts/perf/perf_grid_matrix.sh --label head

# 3. Diff. Output is markdown — paste it into the PR body.
scripts/perf/compare_perf_runs.py \
    save_files/perf/<master-sha>-baseline \
    save_files/perf/<head-sha>-head
```

In a fleet worktree, plain `git checkout master` is unsafe — the main
clone (or another worktree) already has `master` checked out and git
will refuse, or the in-flight feature branch ends up orphaned. Use a
worktree-safe alternative:

- **Throwaway baseline worktree** (recommended):
  ```bash
  git worktree add /tmp/perf-baseline master
  (cd /tmp/perf-baseline && scripts/perf/perf_grid_matrix.sh --label baseline)
  git worktree remove /tmp/perf-baseline
  ```
- **Sibling clean clone** if you already maintain one (e.g.
  `~/src/IrredenEngine.baseline`): run the matrix there and reference
  its `save_files/perf/` output by absolute path when diffing.

## Quick check (no baseline, just see the current state)

```bash
scripts/perf/perf_grid_matrix.sh --quick
scripts/perf/perf_summary.py save_files/perf/<sha>
```

## Matrix size choices

| Flag       | Cells | Time   | When                                            |
|------------|-------|--------|-------------------------------------------------|
| `--quick`  | 2     | ~30s   | Smoke test, single-file change                  |
| (default)  | 12    | ~3 min | Routine PR comparison                           |
| `--full`   | 30    | ~10 min| Deep audit / engine-wide refactor               |

## Targeting a specific demo

```bash
scripts/perf/perf_grid_matrix.sh --target IRLuaPerfGrid
scripts/perf/perf_grid_matrix.sh --target IRShapeDebug      # when added
```

The script auto-resolves the build via `fleet-run`; no need to specify
build paths.

## What lands where

- Per-cell `profile_report.txt` → `save_files/perf/<sha>[-<label>]/<cell-id>.txt`
- Per-cell stdout/stderr → `save_files/perf/<sha>[-<label>]/<cell-id>.log`
- Run metadata → `save_files/perf/<sha>[-<label>]/manifest.json`

`save_files/` is gitignored. The committed perf baselines live in
`docs/perf/baseline_<date>_<phase>.md` (rendered via `perf_summary.py`).

## What the parser surfaces

`compare_perf_runs.py` writes a markdown report with three or four
sections:

1. **Voxel cull effectiveness** — visible/total ratio per cell (added by PR #1019)
2. **Frame timing** — avg + p99 ms per cell with delta vs baseline
3. **GPU stage timing** — per-pass ms across all engine stages
4. **Top CPU systems** — per-cell, sorted by avg ms

Regression threshold defaults: ≥10% slower flagged with `⚠`, ≥5%
faster flagged with `↓`. Pass `--regress-pct N --improve-pct M` to
override.

## When the demo is too slow to finish in the per-cell timeout

The script's `--timeout` flag defaults to 90s per cell. At extreme
zooms with high subdivisions, frames can take >300ms — 120 frames
won't finish in 90s. Either:

- Reduce frames: `--frames 60` (still gives a stable percentile)
- Bump timeout: `--timeout 180`
- Drop subdivisions for that probe: vary `--matrix` rules in the script

If a cell consistently fails (`status: no_report` in the manifest),
that's itself diagnostic information — log it in the PR body, don't
silently drop the cell.

## Where the same data shows up at runtime

- Lua: `ir.render.getPassTimings()`, `ir.render.getVoxelCullStats()` —
  see [`gpu_profiling.md`](gpu_profiling.md).
- HUD: `system_perf_stats_overlay.hpp` reads both surfaces and renders
  the per-stage breakdown live.
- `save_files/profile_report.txt` — written at shutdown by every
  creation that called `IREngine::enableFrameTiming(true)`.

The matrix script aggregates the shutdown reports across cells. The
HUD is the right tool for "is the bug visible right now"; the matrix
is the right tool for "did the bug get worse with this change."

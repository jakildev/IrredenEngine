# docs/perf — perf measurement workflow

This directory holds:

- the **measurement scripts** index for the perf demos (`scripts/perf/`)
- **committed baselines** for major perf phases (e.g. `metal_perf_grid_baseline.md`)
- **per-PR diffs** when an optimization PR captures before/after data

The day-to-day flow is run a matrix on master, run the same matrix on
your dirty tree, diff the two — all three steps are scripts. No GUI
profiler, no per-cell stopwatch.

## The scripts

| Script                                | What it does                                                           |
|---------------------------------------|------------------------------------------------------------------------|
| `scripts/perf/perf_grid_matrix.sh`    | Run `IRPerfGrid` (or `IRLuaPerfGrid`) across a zoom × subdivision matrix |
| `scripts/perf/perf_summary.py`        | One-screen markdown summary of a single run                            |
| `scripts/perf/compare_perf_runs.py`   | Diff two runs as a markdown table for the PR body                      |

All scripts are stdlib-only and run from anywhere in the repo. The
matrix script writes `save_files/perf/<git-sha>[-<label>]/` so multiple
runs coexist without overwriting each other.

## Canonical ritual: before/after a perf change

```bash
# 1. Capture baseline on master before you start.
git checkout master
git pull
scripts/perf/perf_grid_matrix.sh --label baseline

# 2. Switch to your feature branch and run the same matrix.
git checkout claude/my-optimization
scripts/perf/perf_grid_matrix.sh --label head

# 3. Diff. The output is markdown — paste it into the PR body.
scripts/perf/compare_perf_runs.py \
    save_files/perf/<master-sha>-baseline \
    save_files/perf/<head-sha>-head
```

If you only want a quick read on the current state:

```bash
scripts/perf/perf_grid_matrix.sh --quick
scripts/perf/perf_summary.py save_files/perf/<sha>
```

## Matrix size knobs

| Flag       | Cells | Typical use                                  |
|------------|-------|----------------------------------------------|
| `--quick`  | 2     | Smoke test, ~30s total                       |
| (default)  | 12    | Routine PR comparison, ~3 min                |
| `--full`   | 30    | Deep audit, ~10 min                          |

Customize what to vary by editing the matrix arrays at the top of
`perf_grid_matrix.sh` — keep the defaults narrow so PR runs stay fast.

## Config presets

Named, version-controlled test configs live in
`creations/demos/perf_grid/configs/perf/`. Each is a Lua file containing a
`perf_grid` table that overrides demo defaults:

```lua
-- configs/perf/zoom8_full_sub4.lua
perf_grid = {
    zoom = 8,
    subdivision_mode = "full",   -- "none" | "position_only" | "full"
    base_subdivisions = 4,
    -- Any field from the demo's perf_grid config table is valid here:
    -- mode, grid_size, spacing, wave_amplitude, wave_period_seconds, wave_offscreen
}
```

Apply a preset at the CLI:

```bash
fleet-run IRPerfGrid --config-preset configs/perf/zoom8_full_sub4.lua
```

Priority order (highest wins): CLI flags > preset > `config.lua` defaults.

Sweep a preset directory with the matrix script:

```bash
scripts/perf/perf_grid_matrix.sh \
    --presets creations/demos/perf_grid/configs/perf \
    --label my-branch
```

Relative paths for `--presets` are resolved from the engine root.

### Shipped presets

| File | Description |
|---|---|
| `zoom1_none_base1.lua` | Minimal — no subdivision, zoom=1; fast smoke cell |
| `zoom4_full_base1.lua` | Moderate — zoom=4, full subdivision, base=1 |
| `zoom8_full_sub4.lua` | Heavy — zoom=8, full subdivision, base=4 |
| `zoom16_full_base1.lua` | Extreme zoom / cull-audit at zoom=16 |

## CLI flags the scripts depend on

`IRPerfGrid` and `IRLuaPerfGrid` accept these flags (used by the matrix
script). All of these can also be set inside a preset file (except
`--auto-profile` and `--config-preset` itself):

- `--auto-profile <N>` — collect N frames of timing then exit; writes
  `save_files/profile_report.txt`.
- `--config-preset <path>` — load a Lua preset file (relative to the
  exe directory, or absolute). Applied after `config.lua`, before CLI flags.
- `--zoom <F>` — initial camera zoom (snapped to power of 2 in
  `[kTrixelCanvasZoomMin, kTrixelCanvasZoomMax]`).
- `--subdivision-mode <none|position_only|full>` — overrides the
  world-config default for the run.
- `--base-subdivisions <N>` — overrides `voxel_render_subdivisions`
  (clamped 1..16 by the render manager).
- `--mode <voxel_set|sdf>` (IRPerfGrid only) — voxel-pool vs SDF-only
  geometry.
- `--grid-size <N>` — overrides the demo's default grid size.

## Voxel cull stats — the "is culling working?" diagnostic

When `gpu_stage_timing` is enabled, `VOXEL_TO_TRIXEL_STAGE_1` reads the
prior frame's `IndirectDispatchParams.visibleCount` before zeroing the
buffer for the new frame. No explicit fence is required — the driver
serializes the CPU read against the prior frame's already-retired write.
The result is a per-frame sample of
*how many voxels survived the iso-bounds cull*, alongside the pool's
live count. The matrix script surfaces this as the `cull (vis/total)`
column on `perf_summary.py` and a dedicated `voxel cull effectiveness`
table on `compare_perf_runs.py`.

What to look for:

- **Ratio shrinks with zoom**, roughly as `1/zoom²` once the camera is
  past full-screen coverage. If the ratio stays flat or shrinks much
  less than `1/zoom²` while zoom goes up, that's the signature of an
  ineffective viewport cull — frame time grows with the
  subdivision-driven work multiplier while the visible set barely
  changes.
- **Same ratio across two PRs at the same `(zoom, sub_mode, sub_base)`
  cell** is the no-regression baseline for any optimization PR that
  claims to improve culling — pre-PR vs post-PR ratios at the same
  cell.

Lua surface for ad-hoc inspection: `ir.render.getVoxelCullStats()`
returns `{visible, total, samples, avgVisible, avgTotal, maxVisible,
maxTotal}`.

## Committed baselines

When a major phase lands (Phase 1a GPU light volume, T-289 push-at-mutation,
…), commit the matrix output as `docs/perf/baseline_<date>_<phase>.md`
generated via `perf_summary.py`. Subsequent PRs diff against the most
recent baseline. The older free-form file
`docs/perf/metal_perf_grid_baseline.md` remains as historical context.

## Where this is going

A CI gate (`perf/baseline-ci-gate` issue) will eventually re-run the
matrix on every PR touching `engine/render/`, `engine/system/`, or
`engine/math/` and post `compare_perf_runs.py` output as a PR comment.
Until that lands, the human author (or an `/optimize` skill run) does
the comparison locally and pastes the markdown into the PR body.

The current GPU timings use `glFinish()`-style synchronous bracketing
(see `engine/prefabs/irreden/render/gpu_stage_timing.hpp`). That is
accurate but adds a small per-frame overhead — keep `gpu_stage_timing`
off in shipping builds, on during a matrix run. Replacing the sync path
with async `GL_TIMESTAMP` / `MTLCounterSample` queries is tracked in
the `perf/async-gpu-timers` issue.

# docs/perf — perf measurement workflow

This directory holds:

- the **measurement scripts** index for the perf demos (`scripts/perf/`)
- **committed baselines** for major perf phases (e.g. `metal_perf_grid_baseline.md`)
- **per-PR diffs** when an optimization PR captures before/after data

The day-to-day flow is run a matrix on master, run the same matrix on
your dirty tree, diff the two — all three steps are scripts. No GUI
profiler, no per-cell stopwatch.

## The scripts

| Script                                | What it does                                                                       |
|---------------------------------------|------------------------------------------------------------------------------------|
| `scripts/perf/perf_grid_matrix.sh`    | Run `IRPerfGrid` (or `IRLuaPerfGrid`, or both) across a zoom × subdivision matrix |
| `scripts/perf/perf_summary.py`        | One-screen markdown summary of a single run                                        |
| `scripts/perf/compare_perf_runs.py`   | Diff two runs as a markdown table for the PR body                                  |
| `scripts/perf/check_regression.py`    | Same as compare, but exits non-zero on regression — used by CI gate                |
| `scripts/perf/lua_cpp_parity.py`      | Lua-vs-C++ overhead table from a `--target both` run                               |

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

## Lua-vs-C++ parity

`IRLuaPerfGrid` mirrors `IRPerfGrid` but drives the scene through the
codegen/EVAL path instead of hand-written C++ systems. Comparing the two
answers: *is the Lua hot path drifting from the C++ baseline?*

Run both targets in one pass, then generate the parity table:

```bash
scripts/perf/perf_grid_matrix.sh --target both --label parity
scripts/perf/lua_cpp_parity.py save_files/perf/<sha>-parity
```

`lua_cpp_parity.py` prints a markdown table with `ratio = lua_avg /
cpp_avg` and `delta = lua_avg - cpp_avg` per `(zoom, sub_mode,
sub_base)` cell. Cells where the Lua overhead exceeds the threshold
(default 20%) are flagged with ⚠. Paste the output into the PR body
whenever a change touches codegen or the EVAL path.

Options:

```
lua_cpp_parity.py <run_dir> [--gap-pct N] [--cpp NAME] [--lua NAME]
```

- `--gap-pct N` — change the flagging threshold (default 20)
- `--cpp / --lua` — override target names if non-default builds were used

The matrix script stores each cell under
`target=IRPerfGrid,zoom=…` / `target=IRLuaPerfGrid,zoom=…`, so both
sets live in the same output directory and the parity script can cross-
reference them without a second run directory.

## Committed baselines

When a major phase lands (Phase 1a GPU light volume, T-289 push-at-mutation,
…), commit the matrix output as `docs/perf/baseline_<date>_<phase>.md`
generated via `perf_summary.py`. Subsequent PRs diff against the most
recent baseline. The older free-form file
`docs/perf/metal_perf_grid_baseline.md` remains as historical context.

## CI regression gate

`.github/workflows/perf-gate.yml` wires the matrix into CI:

| Trigger | What it does |
|---------|--------------|
| Push to `master` (perf paths) | Runs `--quick` matrix, commits result to `docs/perf/baseline_latest/` |
| PR touching perf paths | Runs `--quick` matrix, compares against `baseline_latest/`, posts a markdown table as a PR comment |

**Pass/fail rules:**

- Any cell where `mean frame avg` regresses by **>10%** fails the check.
  The author must justify or fix before merging.
- Any cell that improves by **>5%** causes the `perf:improved` label to
  be added to the PR automatically.

**Gate script (also usable locally):**

```bash
scripts/perf/check_regression.py <baseline_dir> <head_dir> [--regress-pct N]
# Exit 0: pass. Exit 1: regression detected. Exit 2: usage error.
```

`check_regression.py` wraps `compare_perf_runs.py` — same args, same
markdown table, but also exits non-zero on regression.

**Host stability note:** GitHub Actions `ubuntu-latest` runners are
shared and can have run-to-run timing jitter of 5–15%. The gate uses
`--quick` (2 cells) to reduce wall time and variance. If the gate
produces false positives, lower `--regress-pct` conservatively or
migrate to a dedicated self-hosted Linux runner for stability.

**No baseline yet?** The gate posts a "no baseline" comment and exits
clean. A baseline is committed the next time a perf-relevant change
lands on master.

## GPU timing implementation note

The current GPU timings use async `GL_TIMESTAMP` / `MTLCounterSample`
queries (see T-310). The original `glFinish()`-style synchronous
bracketing added per-frame overhead — keep `gpu_stage_timing` off in
shipping builds, on during a matrix run.

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
| `engine/tools/bin/ir-perf-grid`       | Canonical perf-matrix runner — wraps `perf_grid_matrix.sh` in `ir-acquire benchmark` and splices `ref_ms` + host fingerprint into `manifest.json`. Calibrates on demand via `ir_ref_bench`. |
| `scripts/perf/perf_grid_matrix.sh`    | The matrix loop — `IRPerfGrid` (or `IRLuaPerfGrid`, or both) across a zoom × subdivision matrix. Called via `ir-perf-grid` for CI/perf gating; raw call is fine for ad-hoc local diffs. |
| `scripts/perf/perf_summary.py`        | One-screen markdown summary of a single run                                        |
| `scripts/perf/compare_perf_runs.py`   | Diff two runs as a markdown table for the PR body. Fingerprint-aware: `resolve_baseline` picks `<baseline-root>/<slug>/` for the head's SKU, falling back to a legacy flat root. The root is an argument — in CI it comes from the `perf-baseline` branch, locally it is any directory you pass. |
| `scripts/perf/ci_compare_step.sh`      | The perf gate's PR-path step, extracted from the workflow so it is testable outside Actions. |
| `scripts/perf/tests/test_baseline_layouts.py` | Executed control for baseline resolution + the gate's exit mapping. Runs as the perf-gate job's first step after checkout. |
| `scripts/perf/tests/test_baseline_writer.sh` | Executed control for the `perf-baseline` branch writer and the PR-path reader, driving the shipped workflow step bodies against a local bare origin. Same CI step. |
| `scripts/perf/check_regression.py`    | CI gate — fingerprint-aware regression check. Same fingerprint → gates; different fingerprint or no baseline → informational. |
| `scripts/perf/lua_cpp_parity.py`      | Lua-vs-C++ overhead table from a `--target both` run                               |
| `scripts/perf/million_controls.py`    | The million control as one interleaved matrix: build tree × stage profiling × pose, through `repeat_profile.py` |

All Python scripts are stdlib-only and run from anywhere in the repo. The
matrix script writes `save_files/perf/<git-sha>[-<label>]/` so multiple
runs coexist without overwriting each other.

## Fingerprinted baselines

CI baselines live on the dedicated **`perf-baseline`** branch, under
`docs/perf/baseline_latest/<host-slug>/` (one subdirectory per host SKU
that has produced a baseline). Each subdir contains:

- `manifest.json` — the canonical baseline run manifest (with
  `calibration` block written by `ir-perf-grid`)
- `host.json` — sidecar with the full `ir-host-probe` output that
  produced the slug
- one `<cell-id>.txt` per matrix cell

Read one without checking the branch out:

```bash
git fetch origin perf-baseline
git ls-tree -r --name-only FETCH_HEAD -- docs/perf/baseline_latest
git show FETCH_HEAD:docs/perf/baseline_latest/<slug>/manifest.json
```

The branch is append-only history — never force-pushed — so a baseline's
provenance stays diffable. `master` carries no CI baselines: it is
protected ("changes must be made through a pull request"), so no workflow
can commit one there (#2817).

The CI gate at `.github/workflows/perf-gate.yml` reads the head run's
slug from `manifest.json.calibration.host_slug` and looks up the
matching baseline. Cross-host runs report informational only.

**Coverage is per-SKU, and the hosted runner pool is heterogeneous.**
Measured over 39 baseline-producing runs (#2817): `epyc-7763` 49%,
`epyc-9v74` 31%, `xeon-platinum-8573c` 18%, `xeon-6973p-c` 3%. A run
whose SKU has no baseline is informational, not a failure — which is why
every PR run echoes `perf-gate: head host_slug=<slug>` in its log and in
the PR comment. Seed a missing SKU on demand:

```bash
gh workflow run perf-gate.yml --ref master     # repeat; SKU assignment is random
```

Expect the first comparisons against a fresh baseline to be noisy — a
baseline is a single `--quick` matrix run, so run-to-run variance can
read as a delta. Tuning `--regress-pct` once several SKUs have history
is deliberate follow-up, not part of the gate's contract today.

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

## Repeat a targeted CPU/GPU profile

The [rotation/subdivision audit](rotation-subdivision-audit.md) records the
native timing validity checks, measured matrix and proposed optimization work.

`python3 scripts/perf/repeat_profile.py --output save_files/perf/my-run --
--auto-profile 300 --grid-size 64 --yaw 0.785398` runs the same `IRPerfGrid`
configuration three times through the fleet benchmark lock. It retains fresh
reports, full logs, source state, executable hash and mean/run-range summaries; it fails on a
missing GPU measurement instead of reporting zero cost. Use `--target
IRCanvasStress` before `--` for rotating/attached canvas workloads, with that
demo’s `--auto-profile --auto-screenshot N` exit controls. Canvas stress enables
both CPU and GPU timing for auto-profile.

`--legacy-depth-shadows` on either demo provides the point-caster comparison.
Use the same pose, population and flags in both arms. GPU values are encoder
intervals, not exclusive costs that can always be summed into frame time; see
[the timing contract](../design/gpu-stage-timing-cost-model.md).

The summary carries frame mean, p95 and p99 over all frames, the same three
over the steady frames (the report's `Steady frame time` line, first quarter
excluded) and a tail pooled over every run's steady frames, the full-frame GPU
rows, every GPU stage row the runs share, fixed updates per rendered frame, and
each run's witnessed yaw and per-axis overflow peak and drops. Pose and drops
come from the report's `Run witness` section, which every build type writes,
so a Release run is checked like a Debug one; a run that dropped an overflow
entry, left its `--yaw`, or carries no witness fails. The manifest records the
power source (`host_power`), `host_cpus`, the build tree and its
`CMAKE_BUILD_TYPE`, and per run the start time, battery charge and one-minute
load average: the benchmark lock excludes cooperating builds only, so read the
load before believing a table.

`IRREDEN_BUILD_DIR` selects the tree, as it does for `fleet-build` and
`fleet-run`. The `*-release` configure presets build into `build-release/`
beside the Debug tree:

```bash
cmake --preset macos-release            # or linux-release / windows-release
IRREDEN_BUILD_DIR="$PWD/build-release" fleet-build --target IRPerfGrid
```

Parser regression check: `python3 scripts/perf/test_profile_parser.py`.
Current GPU reports include avg/min/max/sample count; old avg/max reports
remain readable. A row with no writer or no executed work is not proof of a
free feature.

## The million controls

`scripts/perf/million_controls.py` is the objective's headline fixture as one
command: 100³ single-voxel entities in a 128³ pool, zoom 4, FULL subdivision,
frozen wave, at yaw 0°, 45° and a full-turn sweep, with stage profiling on and
off, in every tree
named with `--tree`. Cases run interleaved (forward on odd rounds, reverse on
even) because grouped arms on one host drift by more than the differences
they test; the summary prints per-round means so the drift stays visible, and
the run stops if a binary, the shaders, the runtime scripts or the power source
change under it.

```bash
cmake --preset macos-release      # once; or linux-release / windows-release
fleet-build --target IRPerfGrid
IRREDEN_BUILD_DIR="$PWD/build-release" fleet-build --target IRPerfGrid
python3 scripts/perf/million_controls.py --tree build --tree build-release \
    --output save_files/perf/million-controls
```

[million-controls.md](million-controls.md) holds the reference table later
optimization PRs diff against, with the host conditions it was taken under.

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
| `million.lua` | The million control: 100³ single-voxel entities, `config.voxel_pool_edge = 128`, zoom=4, full subdivision, base=1, stage profiling on |
| `million-profiling-off.lua` | The same scene with `profiling_enabled` and `gpu_stage_timing` off — the arm the 60 fps criterion is read from |

A preset may also carry a `config` table. `World` overlays its keys on
`config.lua`'s, and the engine's pre-init pass reads it too, so
`voxel_pool_edge` sizes the pool from the preset.

## CLI flags the scripts depend on

`IRPerfGrid` and `IRLuaPerfGrid` accept these flags (used by the matrix
script). All of these can also be set inside a preset file (except
`--auto-profile`, `--yaw` and `--config-preset` itself) — the demo-owned ones
under the preset's `perf_grid` table, `--worker-threads` under its `config`
table:

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
- `--worker-threads <N>` — engine-common (every target has it); overrides
  `worker_thread_count`. `-1` auto, `0` inline-serial (no pool — every
  `IRJob` dispatch on the calling thread), `N` an N-worker pool. The axis
  `--threading-baseline` sweeps; `0` is the serial floor, since a one-worker
  pool still has two executors (enkiTS pumps tasks on the waiting thread).
- `--yaw <radians>` (IRPerfGrid only) — initial camera Z-yaw. The profile
  report witnesses the yaw of the first and last rendered frame and the yaw
  travelled between them, and `repeat_profile.py` fails a static-pose run
  whose witness disagrees. Tables
  committed before the unit fix labelled a 0.785° pose as 45°:
  [perf-grid-yaw-unit.md](perf-grid-yaw-unit.md).
- `--yaw-step <radians>` (IRPerfGrid only) — yaw advance per rendered frame;
  frame N renders at `--yaw + (N − 1) × step`, the same poses in every run.
  `repeat_profile.py` checks the first and last pose and the travelled arc
  from the witness, and the flag pins the yaw pivot at the grid centre, because
  with the default pivot the view depends on which frames settled and jumps
  when a sweep lands on a cardinal: [continuous-yaw-sweep.md](continuous-yaw-sweep.md).
- `--pivot-origin` (IRPerfGrid only) — the same pin for a static `--yaw`, so a
  static pose and a swept one frame the scene alike. `million_controls.py`
  passes it on every arm.

## Voxel cull stats — the "is culling working?" diagnostic

When `gpu_stage_timing` is enabled, compact readback reports unique main-list
candidates (`Visible`), separate shadow candidates (`Feeder`), repeated
per-axis list entries (`AxisEntries`), and the producing dispatch's pool-slot
domain (`Total`, including inactive slots). `Ratio` is
`(Visible + Feeder) / Total`; it is candidate retention, not pixel visibility
or a measurement of Hi-Z rejection alone.

Readback consumes the previous compact dispatch before its shared buffers are
reset, using that producer's route and pool size. It can synchronize with the
GPU, particularly between canvases. Startup and unprofiled dispatches are
excluded; the last pending dispatch is not drained at shutdown.

See [voxel cull work units](voxel-cull-work-units.md) for count semantics,
validation, and the old-report compatibility boundary. Compare matching scenes,
camera poses, subdivision modes, and shadow settings. A controlled culling
on/off pair establishes rejection; screenshots establish that visible geometry
was retained. Neither the candidate ratio nor a single timing run establishes
a frame-time improvement.

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
| Push to `master` (perf paths) | Runs `--quick` matrix, appends the result to `docs/perf/baseline_latest/<slug>/` on the `perf-baseline` branch |
| Manual `workflow_dispatch` | Same, on demand — how a missing host SKU gets seeded |
| PR touching perf paths | Runs `--quick` matrix, fetches the baseline from `perf-baseline`, compares, posts a markdown table as a PR comment |

The perf paths include the gate's own sources
(`.github/workflows/perf-gate.yml`, `scripts/perf/**`), so a change to
the gate is exercised by the gate.

**Pass/fail rules:**

- Any cell where `mean frame avg` regresses by **>10%** fails the check.
  The author must justify or fix before merging.
- Any cell that improves by **>5%** causes the `perf:improved` label to
  be added to the PR automatically.
- Baseline resolution belongs to `compare_perf_runs.py:resolve_baseline`
  alone — the workflow hands over a baseline *root* and never tests the
  layout itself. A bash-side layout check is what silently retired the
  gate when T-330 moved the writer to per-slug directories (#2817).
- `check_regression.py` exit ≥ 2 means it could not compare at all. That
  turns the step **red** and posts no comment: an infra failure must not
  read as a perf verdict. This includes a manifest cell whose report is
  missing or does not contain a positive frame-time measurement.
- The matrix itself exits nonzero when any cell produces no report, before a
  push or manual dispatch can replace a measured baseline with an empty one.
  A slug directory filed report-less before that guard existed is purged by
  the writer the next time any SKU files a measured baseline — otherwise the
  exit-2 rule above leaves every PR on that SKU red with no author-side
  remedy.
- Normalization weighs the head against the **baseline run's own `ref_ms`**,
  both readings taken on the same SKU, so the load factor isolates how
  contended the machine was. `ref_target_ms` (a fixed 50 ms) stays in the
  manifest and the host note as information only: the hosted pool calibrates
  at 59–104 ms, so weighing against the target divided every head by
  0.43–0.85 and no regression below roughly 2× could fire (#3471).
- The PR-path reader takes the seed-new (empty root) path only when
  `git ls-remote --exit-code` confirms `perf-baseline` is absent (exit 2).
  Any other failure to reach the branch — an unreachable remote, a fetch
  that dies after the ref was confirmed present — turns the step **red**
  for the same reason: a baseline that exists but could not be retrieved
  must not read as "nothing to compare against".

`scripts/perf/tests/test_baseline_layouts.py` is the executed control for
all of the above (layout resolution across empty / per-slug / legacy-flat
roots, plus the exit mapping), and
`scripts/perf/tests/test_baseline_writer.sh` drives the branch writer and
the PR-path reader against a local bare origin. `test/tools/normalization_test.sh`
covers the calibration helpers and the gate's decision tree. All three run as
the perf-gate job's first step after checkout, before the build.

CI uses 60 frames per quick-matrix cell (45 post-warmup samples) with a
300-second watchdog. Measured on the hosted pool, llvmpipe renders the quick
grid at 490 ms (zoom 1) to 1030 ms (zoom 4) per frame, so a cell costs 45-66 s
and the stock 300-frame window needed five minutes. A cell killed by the
watchdog writes no report at all, so the watchdog is sized from the job's
headroom rather than from a frame-time target. The full
run directory, including each cell's `.log`, is uploaded for seven days as
`perf-run-<workflow-run-id>` so a timeout, crash, or display failure can be
diagnosed from the check run.

**Gate script (also usable locally):**

```bash
scripts/perf/check_regression.py <baseline_dir> <head_dir> [--regress-pct N]
# Exit 0: pass. Exit 1: regression detected. Exit 2: usage or measurement error.
```

`check_regression.py` wraps `compare_perf_runs.py` — same args, same
markdown table, but also exits non-zero on regression.

**Host stability note:** GitHub Actions `ubuntu-latest` runners are
shared and can have run-to-run timing jitter of 5–15%. The gate uses
`--quick` (2 cells) to reduce wall time and variance. If the gate
produces false positives, lower `--regress-pct` conservatively or
migrate to a dedicated self-hosted Linux runner for stability.

**No baseline yet?** The gate posts a "no baseline" comment and exits
clean. A perf-relevant master push or manual dispatch seeds that host on the
`perf-baseline` branch.

## GPU timing implementation note

The current GPU timings use async `GL_TIMESTAMP` / `MTLCounterSample`
queries (see T-310). The original `glFinish()`-style synchronous
bracketing added per-frame overhead — keep `gpu_stage_timing` off in
shipping builds, on during a matrix run.

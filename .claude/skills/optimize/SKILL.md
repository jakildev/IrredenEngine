---
name: optimize
description: >-
  Profiles and improves performance for newly written or modified hot-path
  code in the Irreden Engine — system ticks, render pipeline stages, shaders,
  audio/video processing, and math on the per-frame critical path — by
  running the `scripts/perf/` matrix before and after a change and reporting
  CPU and GPU findings. Use when a change touches any of those paths, or when
  the user says "optimize", "profile", "this is slow", "find the hotspot",
  "check perf", "benchmark this", or "why is this frame slow"; runs BEFORE
  `simplify` because it may add profile blocks and perf-rationale comments
  that simplify keeps.
---

# optimize

Every repeatable measurement is a script in `scripts/perf/`; the skill picks
the script, runs it, reads the data, applies engine-specific fixes, and re-runs
the same script to confirm the win. Engine knowledge that is not a script lives
in `reference/`. A multi-step shell sequence written in prose is a bug in this
skill — extract it ([`reference/script_first.md`](reference/script_first.md)).

## When

The diff touches system ticks (`engine/system/`,
`engine/prefabs/irreden/.../systems/`, per-entity loops in creations), render
stages (`engine/render/`, `engine/prefabs/irreden/render/`, shaders), audio /
video (`engine/audio/`, `engine/video/`, ffmpeg, MIDI), math called from
per-entity loops or per-pixel shader code, or anything the author suspects is
slow. Skip for tests, docs, build/CI, and refactors that preserve hot-path
structure.

## Flow

1. **Triage** — `git diff --stat`, `git diff`. Nothing per-frame, per-entity,
   per-pixel, or per-sample → stop; `simplify` runs instead.
2. **Baseline** before any code change:

   ```bash
   scripts/perf/perf_grid_matrix.sh --label baseline
   ```

   Writes one cell per `(zoom, sub_mode, sub_base)` into
   `save_files/perf/<sha>-baseline/`. `--quick` (2 cells) for a single helper
   or tick body; default (12) or `--full` (30) for broad changes. The script's
   `--help` is the flag reference — never paraphrase its flags in PR bodies.
3. **Smell scan** the diff against
   [`reference/common_bottlenecks.md`](reference/common_bottlenecks.md); flag
   every match.
4. **Profile** when the scan is inconclusive — CPU:
   [`reference/cpu_profiling.md`](reference/cpu_profiling.md); GPU:
   [`reference/gpu_profiling.md`](reference/gpu_profiling.md).
5. **Fix and re-measure**:

   ```bash
   scripts/perf/perf_grid_matrix.sh --label head
   scripts/perf/compare_perf_runs.py \
       save_files/perf/<baseline-sha>-baseline \
       save_files/perf/<head-sha>-head
   ```

   The comparator prints per-cell frame avg/p99, per-pass GPU breakdown, cull
   effectiveness, and top CPU systems, flagging regressions ≥ 10% and
   improvements ≥ 5% (`--regress-pct`, `--improve-pct`). A fix that does not
   move the needle is reverted.
6. **Report** — paste the comparator output into the PR body; the skill's own
   report is:

   ```
   optimize: <N> hot-path file(s) profiled
     CPU hotspots: <count fixed> / <count flagged>
     GPU hotspots: <count fixed> / <count flagged>
     matrix delta: <best-case improvement> at <cell>
     worst regression: <pp> at <cell>
     reference updates: <appended bottleneck patterns>
   ```

   No hotspot found is a valid result — say so.
7. **Self-improve**, in the same PR as the fix: a new pattern → append to
   `reference/common_bottlenecks.md` (pattern, file:line, symptom, fix); a new
   repeatable shell sequence → `scripts/perf/<name>.{sh,py}`, pointed at from
   here; a new partner skill → `reference/partner_skills.md`; a win > 5% frame
   time at the worst-case cell → `reference/big_wins.md`. The reference grows;
   this file does not.

## Coordinating with simplify

Optimize runs first on performance-relevant changes and may add `IR_PROFILE_*`
blocks at hot-path entry points, perf-rationale comments, and cache-friendly
restructuring of a tick. `simplify` keeps those (profiling macros are engine
surface; perf-rationale comments explain a non-obvious why). Non-perf changes
skip optimize.

## Scope

Edits the working tree only; never pushes. Surfaces data and leaves the
cold-path-vs-hot-path judgement to the human (0.1 ms on a 50K-entity hot path
is 5 ms/frame). Never optimises without measuring. Does not drive RenderDoc or
other GUI profilers — hands the capture instructions to the human.

## Reference files

- [`reference/script_first.md`](reference/script_first.md) — scripts over prose
- [`reference/profiling_recipe.md`](reference/profiling_recipe.md) — the matrix → diff loop
- [`reference/cpu_profiling.md`](reference/cpu_profiling.md) — `IR_PROFILE_*` and easy_profiler
- [`reference/gpu_profiling.md`](reference/gpu_profiling.md) — `getPassTimings()`, `getVoxelCullStats()`
- [`reference/common_bottlenecks.md`](reference/common_bottlenecks.md) — the bottleneck catalog
- [`reference/big_wins.md`](reference/big_wins.md) — lessons from the largest wins
- [`reference/partner_skills.md`](reference/partner_skills.md) — what runs before and after

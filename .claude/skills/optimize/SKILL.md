---
name: optimize
description: >-
  Profile and improve performance for newly written or modified
  performance-critical code in the Irreden Engine. Use whenever the
  current change touches a system tick function, a render pipeline
  stage, a shader, audio/video processing, math hot paths, or
  anywhere on the per-frame critical path. Also use when the user
  says "optimize", "profile", "this is slow", "find the hotspot",
  "check perf", "benchmark this", or "why is this frame slow".
  Should run BEFORE simplify when the change is performance-relevant
  — optimize may add profile blocks or comments that simplify would
  otherwise consider extraneous. Reports CPU profiler findings and,
  where GPU profiling infrastructure exists, GPU timer query results.
  Files an issue if the necessary profiling infrastructure is
  missing.
---

# optimize

A performance pass on freshly written or modified hot-path code.

**This skill never tells you how to run a profiling step twice.** Every
repeatable step is a script in `scripts/perf/`. The skill picks the
script, runs it, reads the data, applies engine-specific fixes, and
re-runs the same script to confirm the win. Engine knowledge that
*doesn't* fit in a script lives in `reference/` files this skill points
at — instrumentation recipes, the curated bottleneck catalog, partner
skills, big-win case studies.

If you find yourself writing prose instructions for the next agent on
how to run profiling, stop — extract a script in `scripts/perf/` and
have the skill call it. See [`reference/script_first.md`](reference/script_first.md)
for the principle and where the boundary sits.

## When to invoke

Run optimize when the diff touches:

- System tick functions (`engine/system/`, `engine/prefabs/irreden/.../systems/`, per-entity loops in creations)
- Render pipeline stages (`engine/render/`, `engine/prefabs/irreden/render/`, shaders)
- Audio / video processing (`engine/audio/`, `engine/video/`, ffmpeg paths, MIDI handling)
- Math hot paths (`engine/math/` functions called from per-entity loops or per-pixel shader code)
- Anything the author suspects is slow

Skip optimize when the diff is tests, docs, build/CI, or pure refactors that preserve hot-path structure.

## Flow

### 1. Triage

```bash
git diff --stat
git diff
```

Does any touched file run per-frame, per-entity, per-pixel, or per-sample? If yes → continue. Otherwise stop and let simplify run.

### 2. Baseline

If the change touches `engine/render/`, `engine/prefabs/irreden/render/`, `engine/system/`, or a creation's hot path, capture a baseline before any code change:

```bash
scripts/perf/perf_grid_matrix.sh --label baseline
```

This writes one cell per `(zoom, sub_mode, sub_base)` combo into `save_files/perf/<sha>-baseline/`. The script is the canonical recipe — its `--help` lists the matrix sizes and target overrides. Don't paraphrase the flags in this skill or in PR bodies.

For micro changes (one helper in `engine/math/`, one system tick body), `--quick` is enough — 2 cells, ~30s.

For broad changes, default matrix (12 cells, ~3 min) or `--full` (30 cells, ~10 min).

### 3. Scan the diff for known smells

Cross-reference the diff against the curated catalog in [`reference/common_bottlenecks.md`](reference/common_bottlenecks.md). Each entry has the pattern, a file:line example from the codebase, the symptom in profiler output, and the canonical fix. Auto-flag any match.

The catalog gets seeded with patterns this skill has found in prior runs. Step 7 below is where new patterns land.

### 4. Profile (if smell-scan was inconclusive)

Two surfaces, both pointed at from this skill's references:

- **CPU**: `IR_PROFILE_FUNCTION` / `IR_PROFILE_BLOCK` + easy_profiler — see [`reference/cpu_profiling.md`](reference/cpu_profiling.md).
- **GPU**: per-stage timing via `ir.render.getPassTimings()` and `ir.render.getVoxelCullStats()` — see [`reference/gpu_profiling.md`](reference/gpu_profiling.md).

The reference files have the instrumentation patterns. The skill body does not repeat them.

### 5. Fix and re-measure

Apply the fix. Then:

```bash
scripts/perf/perf_grid_matrix.sh --label head
scripts/perf/compare_perf_runs.py \
    save_files/perf/<baseline-sha>-baseline \
    save_files/perf/<head-sha>-head
```

`compare_perf_runs.py` produces a markdown table with per-cell frame avg/p99, per-pass GPU breakdown, cull effectiveness, and top CPU systems. Flags regressions ≥10% and improvements ≥5% (configurable).

**A fix that doesn't move the needle isn't an optimization.** Revert it and look elsewhere.

### 6. Report

Paste the `compare_perf_runs.py` output into the PR body. The skill's own report is six lines:

```
optimize: <N> hot-path file(s) profiled
  CPU hotspots: <count fixed> / <count flagged>
  GPU hotspots: <count fixed> / <count flagged>
  matrix delta: <best-case improvement> at <cell>
  worst regression: <pp> at <cell>
  reference updates: <appended bottleneck patterns>
```

If profiling found no hotspot, say so — the value here is the confidence, not the changes.

### 7. Self-improve (mandatory)

**This is what makes the skill compound.** Every run that finds a new bottleneck pattern, a new partner-skill cross-ref, or a new repeatable profiling step **must** update the reference in the same PR as the optimization fix:

- New pattern → append to [`reference/common_bottlenecks.md`](reference/common_bottlenecks.md) with file:line example, symptom, and fix sketch.
- New repeatable shell sequence → extract to `scripts/perf/<name>.{sh,py}` and have this skill point at it. Don't grow `SKILL.md` prose.
- New partner skill found useful → add a line to [`reference/partner_skills.md`](reference/partner_skills.md).
- New big win (>5% frame time at the worst-case matrix cell) → add a short case study to [`reference/big_wins.md`](reference/big_wins.md) with the PR link.

If a step was hard, the next agent should have it as a script or a reference entry. The reference grows; the playbook stays small.

## Coordinating with simplify

Optimize runs **first** when the change is performance-relevant.

Optimize may:

- Add `IR_PROFILE_*` blocks at hot-path entry points
- Add explanatory comments documenting non-obvious perf trade-offs (e.g. `// pre-sized to avoid realloc in tick — see PR #N`)
- Restructure a tick function for better cache behavior

Simplify, run after, respects those additions because `IR_PROFILE_*` macros are engine profiling, not debug logs, and perf-rationale comments are kept (they explain non-obvious why).

For non-perf changes, skip optimize and run simplify directly.

## What this skill does NOT do

- **Doesn't replace human profiling judgment.** A 0.1 ms regression in a cold path may be fine; a 0.1 ms regression on the per-entity hot path at 50K entities is 5 ms/frame. Surface the data, let the human decide.
- **Doesn't optimize without measuring.** Speculative optimization is worth less than the readability it costs. Always measure.
- **Doesn't push.** Edits the working tree only.
- **Doesn't run RenderDoc or other GUI profilers.** Hand off the capture instructions and ask the human to run it.
- **Doesn't grow itself.** Prose belongs in `reference/`; recipes belong in `scripts/perf/`. If the playbook is getting longer, something needs to move out.

## See also

- [`reference/script_first.md`](reference/script_first.md) — why scripts beat prose for recurring work
- [`reference/profiling_recipe.md`](reference/profiling_recipe.md) — concrete commands for the matrix → diff loop
- [`reference/cpu_profiling.md`](reference/cpu_profiling.md) — `IR_PROFILE_*` macros and the easy_profiler workflow
- [`reference/gpu_profiling.md`](reference/gpu_profiling.md) — `getPassTimings()`, `getVoxelCullStats()`, async timer queries
- [`reference/common_bottlenecks.md`](reference/common_bottlenecks.md) — curated bottleneck catalog with file:line examples
- [`reference/big_wins.md`](reference/big_wins.md) — case studies of the largest wins
- [`reference/partner_skills.md`](reference/partner_skills.md) — `simplify`, `render-verify`, `render-debug-loop`, etc.

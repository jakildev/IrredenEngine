# CPU profiling — IR_PROFILE_* + easy_profiler

`engine/profile/` wraps easy_profiler: three macros, a histogram scope, and
runtime gates. No-ops in release builds.

## Macros

```cpp
void IRSGlowPulse::tickEntity(C_GlowPulse& glow, C_Color& color) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
}

void some_inner_block() {
    IR_PROFILE_BLOCK("expensive_step", IR_PROFILER_COLOR_RENDER);
    IR_PROFILE_END_BLOCK;
}
```

`IR_PROFILE_FUNCTION(color)` names the scope from `__FUNCTION__`;
`IR_PROFILE_BLOCK(name, color)` / `IR_PROFILE_END_BLOCK` is a manual scope.
Colours are the named `IR_PROFILER_COLOR_*` constants in
`engine/profile/include/irreden/ir_profile.hpp`, never raw ARGB literals.

Wrap the entry point of a new system tick, a new pipeline stage, audio / video
callbacks, and inner sub-blocks worth isolating — not helpers called inside a
per-entity loop, where the per-scope cost is paid N times.

## Sub-tick breakdown

The matrix's "top CPU systems" table names the slow system, not the slow line.
For a once-per-frame tick (a render stage, not a per-entity loop) add
`IR_PROFILE_SCOPE` sub-blocks named `<system>_<region>`:

```cpp
void tick(...) {
    { IR_PROFILE_SCOPE("vs1_clear");  clearCanvasAndDistances(...); }
    { IR_PROFILE_SCOPE("vs1_pos");    /* position upload */ }
    { IR_PROFILE_SCOPE("vs1_color");  /* color upload */ }
}
```

`perf_grid`'s `--auto-profile` dump prints every `IR_PROFILE_SCOPE` that ran
last frame sorted by total ms (`Auto-profile CPU-scope — <name>: <ms>` in each
matrix `.log`), so new sub-scopes appear with no extra wiring. Leave the one or
two that mattered as permanent regression sensors.

## Per-frame histogram

`IR_PROFILE_SCOPE(name)` also feeds `IRProfile::cpuFrameHistogram()` for the
HUD (Lua `ir.render.getCpuPassTimings()`, `ir.render.getCpuPassTiming(name)`);
off by default, two `now()` calls plus one hashmap lookup per scope exit when
on.

## Gates

- `IRProfile::CPUProfiler::instance().setEnabled(bool)` — all `IR_PROFILE_*`
  (easy_profiler path).
- `IRProfile::cpuFrameHistogram().enabled_` — `IR_PROFILE_SCOPE` histogram.
- `IREngine::enableFrameTiming(true)` — per-system timing; the matrix script
  sets it via `--auto-profile N`, and the `World` dtor writes
  `save_files/profile_report.txt` with per-system avg/min/max, which
  `compare_perf_runs.py` parses ("Per-system timing").

Matrix output detects a slowed system; the easy_profiler trace localises what
inside it is slow.

`IRCanvasStress` also accepts `--auto-profile` (no frame-count argument — pair
it with `--auto-screenshot`, whose exit triggers the report). Use it instead of
the perf_grid matrix when the change touches the rotating-set CPU paths
(`REBUILD_GRID_VOXELS` inverse resample, `REBUILD_DETACHED_VOXELS`); perf_grid
spawns no rotating sets, so those systems idle in its report.

## Reading the trace

`.prof` files dump on exit; the human opens them in `profiler_gui`. Give them
the frame number, the colour group to filter on, and ask for the top-N scopes
by total time, not raw duration.

## Wrong tool when

- Frame time grew but "Per-system timing" is flat → GPU-bound:
  [`gpu_profiling.md`](gpu_profiling.md).
- One math helper is suspect → `scripts/perf/run_math_bench.sh` (Catch2
  microbench, JSON report in `save_files/bench/`).
- A stage's avg is fine but its max spikes → allocation or buffer realloc in a
  tick path ([`common_bottlenecks.md`](common_bottlenecks.md)), not a CPU
  hotspot.

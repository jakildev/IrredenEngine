# CPU profiling — IR_PROFILE_* + easy_profiler

The engine wraps easy_profiler under `engine/profile/`. The surface is
three macros, three log routes, and one runtime gate. Compiles to
no-ops in release builds.

## Macros

```cpp
void IRSGlowPulse::tickEntity(C_GlowPulse& glow, C_Color& color) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
    // ... existing logic
}

void some_inner_block() {
    IR_PROFILE_BLOCK("expensive_step", IR_PROFILER_COLOR_RENDER);
    // ... work
    IR_PROFILE_END_BLOCK;
}
```

- `IR_PROFILE_FUNCTION(color)` — name taken from `__FUNCTION__`.
- `IR_PROFILE_BLOCK(name, color)` / `IR_PROFILE_END_BLOCK` — manual scope.
- Always use the named `IR_PROFILER_COLOR_*` constants from
  `engine/profile/include/irreden/ir_profile.hpp`. Raw ARGB hex literals
  are an anti-pattern; the named colors group related blocks visually
  in the easy_profiler timeline.

## Where to wrap

- The entry point of a new system tick function.
- The entry point of a new pipeline stage.
- Audio / video callbacks.
- Inner sub-blocks worth isolating in the timeline (e.g. a per-frame
  upload step inside a tick).

Don't wrap helpers called from a tick — easy_profiler's per-scope cost
adds up if you put it inside the per-entity loop. Wrap the tick itself.

## When the tick itself is the hotspot — sub-tick breakdown

The matrix's "top CPU systems" table tells you *which* system is slow.
It does not tell you *which line of the tick*. When a once-per-frame
system tick (a render-pipeline stage, not a per-entity loop) is the
hotspot, break it down with `IR_PROFILE_SCOPE` sub-blocks:

```cpp
void tick(...) {
    { IR_PROFILE_SCOPE("vs1_clear");  clearCanvasAndDistances(...); }
    { IR_PROFILE_SCOPE("vs1_pos");    /* position upload */ }
    { IR_PROFILE_SCOPE("vs1_color");  /* color upload */ }
    // ...
}
```

This is **not** the "don't wrap helpers" anti-pattern above — that
caveat is about per-*entity*-loop bodies, where the scope cost is paid
N times. A render-stage tick runs **once per frame**, so a handful of
sub-scopes cost a few µs total. Name them `<system>_<region>` so they
group in the dump.

Surfacing them: `perf_grid`'s `--auto-profile` dump prints **every**
`IR_PROFILE_SCOPE` that ran last frame, sorted by total ms (the
`Auto-profile CPU-scope — <name>: <ms>` lines in each matrix `.log`).
So any sub-scope you add shows up in the matrix output with no extra
wiring. Leave the scopes on the one or two regions that turned out to
matter — they are cheap permanent regression sensors on the hot path.

This is how `common_bottlenecks.md` #12 was localized: the matrix
flagged `SingleVoxelToCanvasFirst` as the hotspot, and `vs1_*`
sub-scopes pinned it to the position-upload region (`vs1_pos`: 8 ms at
zoom 1, 56 ms at zoom 8 — same scene).

## Per-frame CPU histogram (the HUD-facing one)

`IR_PROFILE_SCOPE(name)` is a different scope timer that feeds the
per-frame HUD via `IRProfile::cpuFrameHistogram()`. Use it when you want
the HUD to see the cost live, not when you're producing an offline
trace. Lua surface: `ir.render.getCpuPassTimings()` and
`ir.render.getCpuPassTiming(name)`.

The cost is bounded — disabled-by-default, two `now()` calls + one
hashmap lookup per scope exit when enabled.

## Runtime gating

- `IRProfile::CPUProfiler::instance().setEnabled(bool)` — gates all
  `IR_PROFILE_*` (the easy_profiler path).
- `IRProfile::cpuFrameHistogram().enabled_` — gates `IR_PROFILE_SCOPE`
  (the histogram path).
- `IREngine::enableFrameTiming(true)` — flips per-system timing on. The
  matrix script calls this implicitly via `--auto-profile`.

## How the matrix script captures CPU timing

`scripts/perf/perf_grid_matrix.sh` runs the demo with `--auto-profile N`
which calls `IREngine::enableFrameTiming(true)`. The `World` dtor then
writes `save_files/profile_report.txt` containing per-system avg/min/max
ms. `compare_perf_runs.py` parses the "Per-system timing" section and
surfaces the top systems per cell.

The matrix output is the right input for "did my change slow down a
system". The easy_profiler trace is the right input for "what inside
this system is slow". Use both — matrix to detect the regression, trace
to localize.

## Reading the trace

`.prof` files are dumped on exit. The human runs `profiler_gui` to view
them (the agent cannot drive a GUI). When asking the human:

- Point them at the specific frame number of interest.
- Specify which color group (`IR_PROFILER_COLOR_RENDER` etc.) to filter on.
- Ask for the top-N scopes by total time, not by raw duration.

## When CPU profiling is the wrong tool

- Frame time grew but `--- Per-system timing ---` shows nothing
  unusual → GPU-bound, switch to [`gpu_profiling.md`](gpu_profiling.md).
- One specific math helper looks suspicious → microbench (see issue
  #1023; not yet built).
- A render pipeline stage's avg is fine but its max spikes
  occasionally → likely a per-frame GC or buffer realloc, not a CPU
  hotspot. Look for allocation in tick paths
  ([`common_bottlenecks.md`](common_bottlenecks.md)).

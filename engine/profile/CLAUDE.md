# engine/profile/ — logging and CPU profiling

Thin wrappers around spdlog (logging) and easy_profiler (instrumentation).
Exposes one header full of macros that compile to no-ops in release
builds.

## Entry point

`engine/profile/include/irreden/ir_profile.hpp` — macros and free-function wrappers. No
class to instantiate on the caller side; the underlying `LoggerSpd` and
`CPUProfiler` are singletons.

## Logging macros

Three independent sinks, one per logger:

- `IR_LOG_<LEVEL>(fmt, ...)` — **game/client** logger. Use in creation
  code.
- `IRE_LOG_<LEVEL>(fmt, ...)` — **engine** logger. Use in `engine/**`
  code.
- `IRE_GL_LOG_<LEVEL>(fmt, ...)` — **GL debug** logger. Driven by GL
  debug callbacks.

Levels: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`.

Under the hood each macro calls `fmt::runtime(fmt)` so dynamic format
strings work. In `IR_RELEASE` mode every log macro is an empty statement
— don't rely on side effects in arguments.

## Assertion

```cpp
IR_ASSERT(cond, "formatted message {}", value);
```

On failure: logs via the GL API logger at `critical` severity and throws
`std::runtime_error`. In `IR_RELEASE` the macro expands to nothing —
the condition is **not evaluated** at all, not merely silenced.

## Profiling macros

Wrap easy_profiler:

- `IR_PROFILE_FUNCTION(color)` — name taken from `__FUNCTION__`.
- `IR_PROFILE_BLOCK(name, color)` — named scope.
- `IR_PROFILE_END_BLOCK` — manual block close.

Colors are ARGB hex: `0xff0000ff` = pure blue, `0xffff0000` = pure red, etc.

`CPUProfiler::setEnabled(bool)` gates all profile macros. Call it from a
creation's startup if you want profiling off by default.

### `IR_PROFILE_SCOPE(name)` — per-frame CPU histogram

RAII scope timer that records into the per-frame `CpuFrameHistogram`
singleton. Unlike `IR_PROFILE_BLOCK`, which routes into the offline
easy_profiler trace, `IR_PROFILE_SCOPE` is designed for a debug HUD
read every frame:

```cpp
void World::input() {
    IR_PROFILE_SCOPE("input");
    // ...
}
```

Per-stage aggregation: when the same name is recorded multiple times
within a frame the histogram accumulates `totalMs_`, tracks `maxMs_`,
and bumps `count_`. Frame swap happens at the end of `World::render()`
— `IRProfile::cpuFrameHistogram().endFrame()` rotates current → last so
the HUD reads a stable per-frame snapshot.

Cost is bounded: when `cpuFrameHistogram().enabled_` is false (the
default), `ScopeTimer` skips `now()` entirely. When enabled the macro
adds two `steady_clock::now()` calls plus one hashmap lookup per scope
exit — well under a microsecond per scope. After the first few frames
the map has warmed up and the hot path is allocation-free
(heterogeneous `string_view` lookup via transparent hash/eq).

Reader API: `cpuFrameHistogram().lastFrameMs(name)` returns the last
completed frame's total for `name` (0.0 if never recorded). Lua
mirror: `ir.render.setCpuTimingEnabled(true)`,
`ir.render.getCpuPassTimings()`, `ir.render.getCpuPassTiming(name)`
— registered in `engine/world/src/world.cpp::World::setupLuaBindings`
on the `ir.render` table, alongside the GPU-timing surface.

The render-side observer (`GpuStageTimingObserver` in
`engine/prefabs/irreden/render/gpu_stage_timing_observer.hpp`) also
records CPU samples for every tagged render system using the same
mechanism — names map 1:1 to the entries in `gpuStageRegistry()`. The
overlay system at `engine/prefabs/irreden/render/systems/system_perf_stats_overlay.hpp`
reads both sides to render CPU+GPU per-stage ms in the HUD.

## Gotchas

- **Macros are no-ops in release.** Don't put side-effectful calls inside
  log macro args.
- **Dynamic format strings use `fmt::runtime`.** Passing a raw
  user-supplied `std::string` as the format string is a footgun — if it
  contains `{}`, fmt will treat them as format specifiers. Wrap user
  input explicitly.
- **Wrong logger macro = routed to the wrong sink.** Don't use
  `IR_LOG_*` from engine code; it'll land in the game log.
- **`IR_ASSERT` throws.** If you're in a `noexcept` scope, `IR_ASSERT`
  calls `std::terminate`. Prefer early-return plus an `IRE_LOG_ERROR` in
  those spots.
- **easy_profiler has overhead.** Leaving profile blocks enabled in a
  production build costs real cycles. Either gate them with `IR_RELEASE`
  or call `setEnabled(false)` at startup.

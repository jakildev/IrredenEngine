# engine/time/ — fixed-step loop driver

Owns the master clock, the fixed-step accumulator, and per-event profilers
for UPDATE and RENDER. `World::gameLoop()` asks `TimeManager` whether it's
time to run another UPDATE tick, and reads `deltaTime(event)` for dt.

## `IRTime::` public API

- `shouldUpdate()` — true when the UPDATE accumulator has buffered at
  least one frame period (1/kFPS).
- `deltaTime(Events event)` — dt for this event's last tick. For UPDATE,
  returns the fixed step `1.0 / kFPS` (`const`-after-ctor); for RENDER,
  returns the actual wall-clock duration of the last tick.
- `renderFps()`, `updateFps()` — 1-second rolling averages.
- `renderFrameTimeMs()`, `droppedFrames()`, `resetDroppedFrames()`.

## `Events` enum

```
INPUT, UPDATE, RENDER, START, END
```

Used as a template parameter to `beginEvent<T>()` / `endEvent<T>()`, which
`World::gameLoop()` brackets each pipeline stage with. Passed by value to
`deltaTime()` to select which event's dt to return.

## `TimeManager`

Owns two `EventProfiler` instances (one for UPDATE, one for RENDER). Each
tracks:

- Fixed-step accumulator (UPDATE only) — lag from wall-clock vs. target.
- 100-frame rolling history of per-tick durations.
- 1024-frame FPS window (for the `*Fps()` readouts).
- Dropped-frame counter (RENDER only; threshold ≥ 2 frames behind).
- Slow-tick warning cooldown (UPDATE logs a warning if a tick exceeds
  10 ms).

The fixed-step loop pattern:

```cpp
while (running) {
    timeManager.beginEvent<INPUT>();
    executePipeline(INPUT);
    timeManager.endEvent<INPUT>();

    while (shouldUpdate()) {
        timeManager.beginEvent<UPDATE>();
        executePipeline(UPDATE);
        timeManager.endEvent<UPDATE>();
    }

    timeManager.beginEvent<RENDER>();
    executePipeline(RENDER);
    timeManager.endEvent<RENDER>();
}
```

`shouldUpdate()` can return `true` multiple frames in a row if the loop
fell behind — that's the variable catch-up. `skipUpdate()` drops one frame
of lag manually (used by the loading-screen / pause path).

## Gotchas

- **`deltaTime(UPDATE)` is the fixed step, not wall-clock.** Returns
  `m_deltaTimeFixed` (`const double = 1.0 / kFPS`, set once in the
  `EventProfiler<UPDATE>` ctor). The fixed-step loop is what keeps
  simulation deterministic — `shouldUpdate()` runs extra ticks to catch
  up when the loop falls behind, each ticking the same fixed dt. This
  also makes the dt immutable after construction, so PARALLEL_FOR
  bodies can read it from worker threads without synchronization.
  `deltaTime(RENDER)` is wall-clock (variable per frame); use it for
  presentation-frame interpolation, not simulation state.
- **Lag is capped.** `clampUpdateLag(maxTicks)` limits the accumulator
  to at most `maxTicks` frames of debt (default 8, configurable via
  `max_update_ticks_per_frame` in `config.lua`; parsed in
  `engine/world/include/irreden/world/config.hpp`). Without the cap, a
  single slow frame would spiral into hundreds of catch-up ticks.
- **Dropped-frame counter is RENDER-only.** If UPDATE falls behind, it
  catches up via extra ticks; it doesn't "drop" anything.
- **`skipUpdate()` is a footgun.** Skipping ticks silently desyncs any
  system that counts ticks. Use only for loading screens.
- **`setFixedStep(true)` decouples UPDATE from wall-clock.** Set by
  `World::gameLoop` when `IRVideo::isAutoCaptureActive()` (a headless
  `--auto-screenshot` run). `beginMainLoop` then feeds exactly one fixed
  period per render frame, so the loop runs **one UPDATE tick per render
  frame** regardless of wall-clock. This makes per-tick animation
  (`AUTO_SPIN`, particles) advance deterministically across the
  frame-counted capture window; without it the uncapped (vsync-off) capture
  loop races through the window in under one update period and animated
  state is captured at ~identity and non-deterministically. No effect on
  interactive runs (defaults off).
- **FPS windows don't match display frequency.** The 1-second rolling
  window is in engine time, not vsync time. Numbers can look noisy.

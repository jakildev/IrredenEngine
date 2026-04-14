# engine/time/ — fixed-step loop driver

Owns the master clock, the fixed-step accumulator, and per-event profilers
for UPDATE and RENDER. `World::gameLoop()` asks `TimeManager` whether it's
time to run another UPDATE tick, and reads `deltaTime(event)` for dt.

## Entry point

`engine/time/include/irreden/ir_time.hpp` — exposes `IRTime::` free
functions:

- `getTimeManager()`.
- `shouldUpdate()` — true when the UPDATE accumulator has buffered at
  least one frame period (1/kFPS).
- `deltaTime(Events event)` — actual wall-clock dt for this event's last
  tick.
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

## Internal layout

```
engine/time/
├── include/irreden/
│   ├── ir_time.hpp             — public facade
│   └── time/
│       ├── ir_time_types.hpp   — Events enum, aliases, constants
│       ├── time_manager.hpp    — TimeManager class
│       └── event_profiler.hpp  — EventProfiler<T> template specializations
└── src/                         — TimeManager impl
```

## Gotchas

- **`deltaTime(UPDATE)` is wall-clock, not fixed.** It's the actual time
  the last UPDATE tick took, not `1.0 / kFPS`. Good for physics
  damping and decay, but **do not** use it to advance simulation state
  you expect to be deterministic — that's why the fixed-step loop exists.
- **Lag can cascade.** If one frame takes 50 ms, the next loop iteration
  runs 3 UPDATE ticks back-to-back. Budget accordingly.
- **Dropped-frame counter is RENDER-only.** If UPDATE falls behind, it
  catches up via extra ticks; it doesn't "drop" anything.
- **`skipUpdate()` is a footgun.** Skipping ticks silently desyncs any
  system that counts ticks. Use only for loading screens.
- **FPS windows don't match display frequency.** The 1-second rolling
  window is in engine time, not vsync time. Numbers can look noisy.

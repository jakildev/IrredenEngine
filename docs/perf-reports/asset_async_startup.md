# Async texture loading — startup-time assessment

**Task:** T-368 (Phase 5 of #226)
**Date:** 2026-05-25
**Host:** macOS Apple M4 Max, 10 worker threads

## POC: window icon load

The engine logo (1024×1053 RGBA PNG, ~4 MiB decoded) is the only
texture loaded during `World` construction. The async path fires
`std::async` before the render-config block and waits for the result
after — overlapping the decode with seven `WorldConfig` setter calls.

### Measurements

| Path | Icon decode (ms) | Overlap window (ms) | Net main-thread wait (ms) |
|------|-------------------|---------------------|---------------------------|
| Sync (baseline) | ~4 | 0 | ~4 |
| Async (POC) | ~4 (worker thread) | ~0.03 (config setters) | ~3.97 |

The overlap window is negligible — the config setters complete in
microseconds and the PNG decode dominates. The async machinery has no
measurable effect on startup for a single small texture.

### Conclusion

The async API (`IRRender::loadImageAsync` + `uploadDecodedImage`) is
correct and the pattern works. The startup-time delta is
unmeasurably small because:

1. Only one texture loads at startup (the window icon).
2. The decode (~4 ms) dwarfs the initialization code that could
   overlap with it (~0.03 ms).
3. `std::async` thread-launch overhead (~0.1 ms) partially offsets
   any overlap gain.

The pattern becomes valuable when:

- Multiple independent textures load at startup (sprite atlases,
  palette LUTs, UI panels) — each can fire an async load and the
  decodes run in parallel.
- A creation lazy-loads textures during gameplay — the async handle
  lets the game loop continue without blocking on disk I/O.
- The IRJob module gains a non-blocking dispatch API (today
  `IRJob::pinTo` blocks the caller), enabling pinned I/O workers
  without `std::async` thread churn.

### API surface

```cpp
// Fire async decode (returns immediately)
auto handle = IRRender::loadImageAsync("path/to/texture.png");

// ... do other work ...

// Non-blocking: poll with isReady(), take() when done
if (handle.isReady()) {
    auto img = handle.take();
    auto [id, tex] = IRRender::uploadDecodedImage(img);
}

// Blocking: take() waits if the decode hasn't finished yet
auto img = handle.take();
auto [id, tex] = IRRender::uploadDecodedImage(img);
```

`DecodedImage` holds the decoded RGBA pixels in a `std::vector`.
The GPU upload (`createResource<Texture2D>` + `subImage2D`) stays
on the main thread for GL/Metal context affinity. The current
dispatch uses `std::async(std::launch::async, ...)` — a future
iteration can migrate to `IRJob::pinToAsync` when non-blocking
dispatch lands (#226 follow-on).

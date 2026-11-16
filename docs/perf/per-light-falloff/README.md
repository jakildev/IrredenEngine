# Per-light falloff cost

PR #5036 preserves the single-radius light-volume path and adds a
mixed-radius path. This record grades the unchanged path against parent
`345dbbf251fe` and head `13ceaa3744b5` on Metal (M4 Max, macOS 26.5.2,
AC power).

## Method

Five parent/head pairs ran in interleaved order through the guarded benchmark
lease. Each arm used the Release `IRPerfGrid` million control, 300 frames,
frozen wave, yaw zero, pivot pinned at the origin, and stage profiling enabled:

```text
python3 scripts/perf/repeat_profile.py --target IRPerfGrid --release \
  --output <pair-arm> --repeats 1 --timeout 180 -- \
  --config-preset configs/perf/million.lua --wave-freeze --no-overlay \
  --pivot-origin --yaw 0 --auto-profile 300
```

Every retained manifest records `quiet=guarded`, `host_power=AC Power`, the
parent binary SHA-256
`36a3c0ba7c20a604b130e3f38f33373e5ed35bcd0f7a01fd2dcc109524c573a2`,
or the head binary SHA-256
`9357095f40081bdabae002adbdc68c755ef4812038969a186cccd4b96c31b99a`.
The earlier attempted pair whose head manifest recorded host load 45.14 was
excluded before grading and replaced as a whole; no individual reading from
that pair appears in `comparison.json`.

## Result

The identity-lane rule-(i) limit is 1.05x or +0.010 ms. All frame rows and
`computeLightVolume` pass by ratio. The decisive stage row is 0.3334 to
0.3484 ms, or 1.0450x. The frame-average row is 28.932 to 29.022 ms
(1.0031x), and the steady-frame-average row is 27.984 to 28.038 ms
(1.0019x). `comparison.json` retains every paired input and all six frame
summary rows.

The mixed-radius fixture was recorded separately, not used as a gate:

```text
fleet-run --release --timeout 180 IRLightingPerLightFalloff --auto-profile 300
```

That guarded run exited cleanly. Its two-light `computeLightVolume` exclusive
mean was 3.448 ms over 299 samples; frame average was 8.47 ms and steady frame
average was 8.40 ms.

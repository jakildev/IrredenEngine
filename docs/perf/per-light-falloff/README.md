# Per-light falloff cost

This record grades the unchanged single-radius path at radius-class head
`83d312a7e` against parent `345dbbf251fe`. Both refs precede `a661037da`, as
required by issue #4967 plan correction A4.

The replacement path schedules one shared-falloff pass per radius class. The
radius-32/radius-8 fixture therefore issues 40 propagation iterations per
canvas per frame. Single-class frames still issue exactly their class radius.

## Method

Five parent/head pairs ran in interleaved order through the guarded benchmark
lease. Each arm used a Release binary on Metal (M4 Max, macOS 26.5.2, AC
power), the million control, 300 frames, frozen wave, yaw zero, and a pinned
pivot:

```text
python3 scripts/perf/repeat_profile.py --target IRPerfGrid \
  --output <pair-arm> --repeats 1 --timeout 180 -- \
  --config-preset configs/perf/million.lua --wave-freeze --no-overlay \
  --pivot-origin --yaw 0 --auto-profile 300
```

Every retained manifest records `quiet=guarded`, `host_power=AC Power`, parent
binary SHA-256 `36a3c0ba7c20a604b130e3f38f33373e5ed35bcd0f7a01fd2dcc109524c573a2`,
or head binary SHA-256
`db472a3dceb1b28b23aa8d56cad1c78d7997395ee1808a6fb2832a1a24aad435`.
Pair 4 was replaced in full before grading: its head had an isolated 42.28 ms
frame p99 while its GPU rows remained flat. No reading from that pair appears
in `comparison.json`.

## Result

All six frame rows pass identity rule (i). Frame average is 28.904 to 28.884
ms (0.9993x), steady-frame average is 27.932 to 27.894 ms (0.9986x), and the
largest ratio is frame p99 at 1.0194x. `computeLightVolume` is 0.3330 to
0.3306 ms (0.9928x). `comparison.json` retains every paired input.

The mixed-radius fixture was recorded separately, not used as a gate:

```text
fleet-run --release --timeout 180 IRLightingPerLightFalloff --auto-profile 300
```

The guarded run exited `RESULT=CLEAN`. Its two-light `computeLightVolume`
exclusive mean was 3.461 ms over 299 samples; frame average was 8.57 ms and
steady-frame average was 8.48 ms.

# Stage observer lifetime controls

Host: macOS / Metal, freshly built `IRCanvasStress`, 2048 × 1152 offscreen
framebuffer. Source is this change on parent `3d28ef4194afbcfa1b67d6c51e4bddc7d230ade5`.
The [manifest](comparison.json) records changed-header hashes, all PNG hashes,
commands, camera poses, clean exits and test results.

| Control | Frames compared | Exact PNG matches |
|---|---:|---:|
| Beauty, stage timing disabled/enabled | 12 | 12 |
| Shadow diagnostic, stage timing disabled/enabled | 12 | 12 |
| Shadow diagnostic against the previous cleanup's captures | 12 | 12 |

All four accepted runs exited with `RESULT=CLEAN`. The previous cleanup's
[capture evidence](../shadow-fallback-cleanup/README.md) identifies its source.
This is a behavior-preservation check, not proof that every inherited surface
artifact is correct. The revoxelized span-cap warning (12 of 1740 covered cells
dropped from a 1728-cell span) occurs in both timing modes and the previous run.

## Reproduction

Build with `fleet-build --target IRCanvasStress -j2`, then run:

```text
fleet-run IRCanvasStress --auto-screenshot 120 --no-auto-rotate --no-spin
```

For the enabled arm, append `--config-preset` and the absolute path to
[timing-on.lua](timing-on.lua). For shadow diagnostics, append
`--debug-overlay shadow` to both arms. The sequence includes camera yaw 0°, 45°
and 30°. Profiling counters are enabled through configuration; these runs do not
provide benchmark results or independently verify stage sample counts.

The initial `--auto-profile` attempt waited for an exclusive benchmark lease and
was cancelled before producing captures. The accepted runs use normal GPU capture
leases. No clip or temporal-stability claim is included in this lifetime change.

## Representative frames: yaw 30°

| Output | Timing disabled | Timing enabled |
|---|---|---|
| Beauty | [Disabled](beauty-timing-off.png) | [Enabled](beauty-timing-on.png) |
| Shadow diagnostic | [Disabled](shadow-timing-off.png) | [Enabled](shadow-timing-on.png) |

## Executable lifetime controls

`IrredenEngineTest` builds successfully. The following focused selection passes
all 28 tests, including nine new cases:

```text
fleet-run IrredenEngineTest --gtest_filter=GpuTimestampPollTest.*:GpuTimestampRingTest.*:TickObserverLookupTest.*:RegisterSystemTest.*
```

The five new timestamp regressions fail when compiled with the parent observer
header, covering duplicate tags, changed tags, partial allocation, clearing and
same-address reconstruction, and separate live managers. Removing the new lookup
thread guard makes its worker-rejection test fail too. These controls use temporary
include overlays and the built engine libraries; they do not modify production
sources. The separate-live-managers control also fails in its own fresh process,
independently of the stale cache left by the clearing control.

The native runs cover normal initialization, drawing and shutdown. The headless
tests isolate manager and timestamp ownership; neither establishes a full
multi-World engine restart contract. Windows/OpenGL execution remains unverified
on this host. No rendering algorithm, sampling boundary, blur or speedup changes
are claimed.

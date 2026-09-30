# Static CanvasStress camera witness

The [native report](run-1.txt) and [manifest](manifest.json) verify the profile
runner's static-camera acceptance path on Metal Debug. This is a smoke check of
instrumentation, not a before/after performance comparison. The manifest pins
the clean tested source head, binary, shaders, runtime scripts and host conditions.

The run uses a bare `--auto-screenshot` and inline `--sweep-frames=2,480`:

```bash
python3 scripts/perf/repeat_profile.py --target IRCanvasStress --output /tmp/static-camera-profile --repeats 1 -- --only shadowbox,floor --probe-grid --probe-floor-mode grid --probe-floor-span 32 --no-spin --no-auto-rotate --no-ao --pivot-origin --yaw 0.39269908 --zoom 1.6 --subdivisions 1 --auto-profile --sweep-frames=2,480 --auto-screenshot
```

Yaw stayed at 22.5 degrees with zero accumulated travel. Rendered zoom stayed at
2.0: first, last, minimum and maximum all agree, despite the requested 1.6.
The run exited cleanly with fresh GPU measurements and no dropped overflow
entries. The [summary](summary.md) retains observed timing without claiming a
speedup or population-scale result.

Failure coverage is deterministic: the C++ witness tests record `2 → 1 → 2`
and `2 → 32 → 2`; Python tests parse excursions and malformed bounds, invoke the
runner, verify failure is retained in its manifest, and require that no accepted
summary is written. The static CanvasStress lane requires range-capable reports.
Legacy IRPerfGrid reports remain parseable without inventing zoom bounds.

The guard does not measure translation or pitch/roll. Moving yaw/pan shot tables,
the full-rotation driver and the default screenshot suite are outside its static
acceptance scope.

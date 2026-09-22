# One pose from two first frames

`IRPerfGrid --wave-freeze --no-overlay --auto-profile 75 --config-preset
configs/perf/million.lua --yaw 0.816814 --capture-frame 60` plus the arm's own
flags, Release, macos, Metal, AC power, host load 3.5 to 3.9 (`uptime` as each
run returned). Every arm holds 46.8° from frame 2 to frame 75 and is captured
after frame 60; only the yaw of frame 1 and the pivot differ. These four ran
through `fleet-run` directly, because `repeat_profile.py` refuses
`--capture-frame`, so they have reports and no manifests. Each report's
`Camera pivot:` line says which pivot it rendered with.

| Arm | Flags | Explicit pivot frames | Visible candidates at the held view | Steady p50 ms | Lit pixels | Capture SHA-256 |
|---|---|---:|---:|---:|---:|---|
| `default-pivot-from-cardinal` | `--default-pivot --yaw-first-frame=0` | 0 of 75 | 482,966 | 28.28 | 50.0% | `109c921343c5bf4d` |
| `default-pivot-from-rotated` | `--default-pivot --yaw-first-frame=0.816814` | 0 of 75 | 626,223 | 31.95 | 62.9% | `82c04ae046177f3d` |
| `pinned-from-cardinal` | `--yaw-first-frame=0` | 75 of 75 | 909,433 | 39.18 | 89.8% | `f42cdb630fb35288` |
| `pinned-from-rotated` | `--yaw-first-frame=0.816814` | 75 of 75 | 909,433 | 39.32 | 89.8% | `f42cdb630fb35288` |

The visible count is the report's mean with its one off-view sample taken out:
each run's 74 cull samples include one from a different view (the 0° frame,
786,156, in the two cardinal arms; a first-frames sample at the fallback
pivot, 909,433, in `default-pivot-from-rotated`), so the reports' own means
read 487,063, 630,050, 907,767 and 909,433. The milliseconds are the steady
p50, which one frame does not move; the capture's readback makes frame 60
cost 135 to 147 ms in every arm, and it is each report's steady p99 and
maximum. These arms run with stage profiling on and are not comparable with
the profiling-off tables in the document above them.

`scripts/render-compare.py` on the captures, which are committed full-size
under `docs/pr-screenshots/claude/million-entity-render-visible-set-identity/`:
the default-pivot pair matches on 80.04% of pixels with a maximum delta of 134
(the scene is translated about 330 pixels); the pinned pair matches on 100%
with a maximum delta of 0.

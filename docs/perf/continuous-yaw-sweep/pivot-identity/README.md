# One pose from two first frames

`IRPerfGrid --wave-freeze --no-overlay --auto-profile 75 --config-preset
configs/perf/million.lua --yaw 0.816814 --capture-frame 60` plus the arm's own
flags, Release, macos, Metal, head `11de3f891` plus this change. Every arm
holds 46.8° from frame 2 to frame 75 and is captured after frame 60; only the
yaw of frame 1 and the pivot differ. These four ran through `fleet-run`
directly, so they have reports and no manifests.

| Arm | Flags | Visible candidates, mean | Axis entries, mean | Median ms | Capture SHA-256 |
|---|---|---:|---:|---:|---|
| `default-pivot-from-cardinal` | `--default-pivot --yaw-first-frame=0` | 487,063 | 1,429,318 | 28.7 | `109c921343c5bf4d` |
| `default-pivot-from-rotated` | `--default-pivot --yaw-first-frame=0.816814` | 630,050 | 1,890,150 | 32.0 | `82c04ae046177f3d` |
| `pinned-from-cardinal` | `--yaw-first-frame=0` | 907,767 | 2,691,430 | 39.1 | `f42cdb630fb35288` |
| `pinned-from-rotated` | `--yaw-first-frame=0.816814` | 909,433 | 2,728,299 | 39.3 | `f42cdb630fb35288` |

`scripts/render-compare.py` on the full-size captures: the default-pivot pair
matches on 80.04% of pixels with a maximum delta of 134 (the scene is
translated on screen); the pinned pair matches on 100% with a maximum delta
of 0. The pinned cardinal arm's mean counts are a hair lower because they
include its one frame at 0°. Half-size captures:
`docs/pr-screenshots/claude/million-entity-render-visible-set-identity/`.

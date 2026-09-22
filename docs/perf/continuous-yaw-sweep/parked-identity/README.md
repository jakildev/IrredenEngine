# The parked per-axis set: pixel identity

`IRPerfGrid --wave-freeze --no-overlay --config-preset
configs/perf/million-profiling-off.lua` plus the arm's own flags, Release, macos,
Metal, battery power at 82%, host load about 3.0 with the fleet live, one
screenshot per run through `fleet-run` directly (`repeat_profile.py` refuses
`--capture-frame`), so these have reports and no manifests. `parked-*` arms ran
the binary `18ab1d21…` of this branch; `base-*` arms ran master `79ca9e3c6`'s
Release binary `2d01e5b1…`, copied out of the tree before the rebuild. Each
report's `--- CPU phase timing ---` table carries the `PerAxisCanvas::*` rows
that say which lifecycle the run took, and its `Run witness` the pose.

| Arm | Flags | Lifecycle rows | Capture SHA-256 |
|---|---|---|---|
| `parked-cardinal-parked` | `--yaw-first-frame 0.5 --yaw 0 --auto-profile 70 --capture-frame 60` | Allocate 1, Park 1 | `d145106255ee8f8d` |
| `parked-cardinal-fresh` | `--yaw 0 --pivot-origin --auto-profile 70 --capture-frame 60` | none | `d145106255ee8f8d` |
| `base-cardinal-parked` | `--yaw-first-frame 0.5 --yaw 0 --auto-profile 70 --capture-frame 60` | Allocate 1, Release 1 | `d145106255ee8f8d` |
| `parked-unpark-sweep` | `--yaw 0 --yaw-step 0.020943951 --auto-profile 80 --capture-frame 77` | Allocate 1, Park 1, Unpark 1 | `34449f70093ec346` |
| `parked-static-91p2` | `--yaw 1.591740276 --pivot-origin --auto-profile 80 --capture-frame 77` | Allocate 1 | `34449f70093ec346` |
| `base-unpark-sweep` | `--yaw 0 --yaw-step 0.020943951 --auto-profile 80 --capture-frame 77` | Allocate 2, Release 1 | `8e1b1ab979d07c2f` |

The three `cardinal` arms hold 0° from frame 2 (the two `--yaw-first-frame`
arms render frame 1 at 28.648°, so the set is allocated and then parked or
released on frame 2) and are captured after frame 60, inside the 120-frame
parked window. The two `sweep` arms step 1.2° a frame from 0°, cross 90° on
frame 76 and are captured after frame 77, at 91.2°; `parked-static-91p2` holds
91.2° from frame 1.

The captures are committed full-size under
`docs/pr-screenshots/claude/million-entity-render-parked-per-axis-set/`, one
file per distinct hash: `cardinal-0deg-parked-set-resident.png`
(`d145106255ee8f8d`, all three cardinal arms),
`rotated-91p2deg-unparked-and-settled.png` (`34449f70093ec346`, the unparked
and the settled arm) and
`rotated-91p2deg-control-first-frame-after-reallocation.png`
(`8e1b1ab979d07c2f`, master's first frame on a fresh allocation), with
`rotated-91p2deg-control-vs-parked-diff.png` the per-byte difference of the
last two: 32,300 of 3,686,400 pixels differ by 1 to 7 and 304 by 32 to 54.

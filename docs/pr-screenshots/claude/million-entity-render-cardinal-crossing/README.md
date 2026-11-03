# Pixel control for the per-axis lifecycle timing

The engine change is two `steady_clock` reads around the existing `allocate`
and `release` calls in `syncAllocationToCameraYaw`, recorded into the run
witness. No GPU call, binding or predicate changes, so there is no before and
after to show.

Control: `python3 scripts/render-verify.py --target IRShapeDebug`, macos-debug,
Metal, on this tree: all 33 checks pass at 100.0% with a maximum delta of 0.
Its pivot shots yaw the camera through cardinals, so both timed calls run.

`IRCanvasStress` is not used as the control here: its macos-debug references
are stale on master (issue 3622 in the tracker), so it fails 7 of 11 with or
without this change.

The release-disabled runs in `docs/perf/continuous-yaw-sweep/` are a timing
experiment from a local patch that was never committed.

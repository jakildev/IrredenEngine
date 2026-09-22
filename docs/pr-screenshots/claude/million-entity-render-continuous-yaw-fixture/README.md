# Visual evidence for the continuous-yaw fixture

This slice changes no render path. `IRPerfGrid` registers its `YawSweep` system
only when `--yaw-step` is non-zero, so a run without the flag builds the same
render pipeline as before and does not touch the camera pivot, and the engine
change is two vectors in the profile report. There is no before and after to capture.

What the sweep looks like is the figure: frame time and fixed updates per
frame across a full turn through the cardinals at one million entities, with
the yaw pivot pinned at the grid centre and with the default pivot, drawn from
the committed reports under `docs/perf/continuous-yaw-sweep/`.

![Frame time and fixed updates per frame for the same sweep, pinned and unpinned](sweep-frames.svg)

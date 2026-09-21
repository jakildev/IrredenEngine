# One pose, two first frames

`IRPerfGrid` at one million entities, held at a yaw of 46.8° and captured
after frame 60. Half-size copies of 2560 by 1440 captures; hashes and counts
of the originals are in `docs/perf/continuous-yaw-sweep/pivot-identity/README.md`.

With the engine's default yaw pivot the view depends on the first frame: the
scene sits about 330 pixels further left when frame 1 was rendered on the
cardinal, and the visible count is 487,063 against 630,050.

| Default pivot, frame 1 at 0° | Default pivot, frame 1 at 46.8° |
|---|---|
| ![default pivot from a cardinal first frame](default-pivot-from-cardinal.png) | ![default pivot from a rotated first frame](default-pivot-from-rotated.png) |

With the pivot pinned at the grid centre both first frames give this capture,
byte for byte:

![pinned pivot, either first frame](pinned-from-cardinal.png)

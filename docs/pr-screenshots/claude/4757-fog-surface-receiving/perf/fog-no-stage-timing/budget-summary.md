Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| cardinal-zoom4-overlap16--surface0-prepass0 | 3 | 24.617 / 27.147 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16--surface0-prepass1 | 3 | 25.867 / 28.104 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16--surface1-prepass0 | 3 | 25.786 / 28.170 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16--surface1-prepass1 | 3 | 25.248 / 27.500 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface0-prepass0 | 3 | 22.016 / 22.317 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface0-prepass1 | 3 | 22.065 / 22.666 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface1-prepass0 | 3 | 21.974 / 22.248 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface1-prepass1 | 3 | 22.068 / 22.506 | 810 / 810 | 0 / 0 |

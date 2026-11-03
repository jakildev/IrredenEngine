Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| cardinal-zoom4-overlap16--surface0-prepass0 | 3 | 26.864 / 28.907 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16--surface0-prepass1 | 3 | 27.319 / 29.956 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16--surface1-prepass0 | 3 | 26.959 / 30.183 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16--surface1-prepass1 | 3 | 26.573 / 29.000 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface0-prepass0 | 3 | 27.749 / 29.497 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface0-prepass1 | 3 | 26.490 / 29.042 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface1-prepass0 | 3 | 26.895 / 29.896 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface1-prepass1 | 3 | 27.017 / 28.769 | 810 / 810 | 0 / 0 |

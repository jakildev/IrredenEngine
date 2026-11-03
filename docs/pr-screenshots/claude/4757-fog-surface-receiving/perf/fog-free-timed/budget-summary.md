Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| cardinal-zoom4-overlap16--surface0-prepass0 | 3 | 26.168 / 28.616 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16--surface0-prepass1 | 3 | 26.868 / 29.437 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16--surface1-prepass0 | 3 | 27.936 / 30.044 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16--surface1-prepass1 | 3 | 26.934 / 29.578 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface0-prepass0 | 3 | 26.660 / 28.595 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface0-prepass1 | 3 | 26.822 / 29.210 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface1-prepass0 | 3 | 26.803 / 28.878 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16--surface1-prepass1 | 3 | 26.295 / 28.058 | 810 / 810 | 0 / 0 |

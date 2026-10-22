Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| per-axis-zoom4-overlap0--surface0-prepass0 | 2 | 8.760 / 19.673 | 12 / 360 | 0 / 0 |
| per-axis-zoom4-overlap0--surface0-prepass1 | 2 | 8.729 / 20.778 | 16 / 360 | 0 / 0 |
| per-axis-zoom4-overlap0--surface1-prepass0 | 2 | 17.708 / 20.633 | 47 / 360 | 0 / 0 |
| per-axis-zoom4-overlap0--surface1-prepass1 | 2 | 17.758 / 20.970 | 45 / 360 | 0 / 0 |

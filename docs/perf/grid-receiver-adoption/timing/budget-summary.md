Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| per-axis-zoom4-receivers-subdiv1--surface0-prepass0 | 2 | 8.514 / 8.704 | 2 / 540 | 629 / 0 |
| per-axis-zoom4-receivers-subdiv1--surface0-prepass1 | 2 | 8.508 / 8.613 | 1 / 540 | 629 / 0 |
| per-axis-zoom4-receivers-subdiv1--surface1-prepass0 | 2 | 18.285 / 20.298 | 93 / 540 | 629 / 0 |
| per-axis-zoom4-receivers-subdiv1--surface1-prepass1 | 2 | 17.458 / 18.014 | 34 / 540 | 629 / 0 |

Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| per-axis-zoom4-overlap0--surface0-prepass0 | 2 | 8.429 / 8.679 | 1 / 360 | 0 / 0 |
| per-axis-zoom4-overlap0--surface0-prepass1 | 2 | 8.443 / 8.670 | 1 / 360 | 0 / 0 |
| per-axis-zoom4-overlap0--surface1-prepass0 | 2 | 8.460 / 8.711 | 2 / 360 | 0 / 0 |
| per-axis-zoom4-overlap0--surface1-prepass1 | 2 | 8.412 / 8.533 | 2 / 360 | 0 / 0 |
| per-axis-zoom4-overlap16--surface0-prepass0 | 2 | 22.919 / 24.400 | 360 / 360 | 0 / 0 |
| per-axis-zoom4-overlap16--surface0-prepass1 | 2 | 24.556 / 26.859 | 360 / 360 | 0 / 0 |
| per-axis-zoom4-overlap16--surface1-prepass0 | 2 | 23.643 / 27.378 | 360 / 360 | 0 / 0 |
| per-axis-zoom4-overlap16--surface1-prepass1 | 2 | 22.653 / 23.552 | 360 / 360 | 0 / 0 |
| cardinal-zoom4-overlap16--surface0-prepass0 | 2 | 21.910 / 22.958 | 360 / 360 | 0 / 0 |
| cardinal-zoom4-overlap16--surface0-prepass1 | 2 | 22.547 / 23.234 | 360 / 360 | 0 / 0 |
| cardinal-zoom4-overlap16--surface1-prepass0 | 2 | 21.940 / 22.733 | 360 / 360 | 0 / 0 |
| cardinal-zoom4-overlap16--surface1-prepass1 | 2 | 22.384 / 23.442 | 360 / 360 | 0 / 0 |

Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| cardinal-zoom1-overlap0 | 3 | 8.880 / 9.389 | 0 / 810 | 0 / 0 |
| cardinal-zoom1-overlap8 | 3 | 22.687 / 24.191 | 810 / 810 | 0 / 0 |
| cardinal-zoom1-overlap16 | 3 | 23.496 / 25.864 | 810 / 810 | 0 / 0 |
| cardinal-zoom1-overlap8-subdiv4 | 3 | 22.539 / 23.206 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap0 | 3 | 8.567 / 8.819 | 1 / 810 | 0 / 0 |
| cardinal-zoom4-overlap8 | 3 | 22.208 / 23.380 | 561 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16 | 3 | 24.055 / 25.450 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap8-subdiv4 | 3 | 22.248 / 23.151 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16-no-shadows | 3 | 23.788 / 26.062 | 810 / 810 | 0 / 0 |
| cardinal-zoom4-overlap16-unlit | 3 | 24.197 / 25.867 | 810 / 810 | 0 / 0 |
| per-axis-zoom1-overlap0 | 3 | 22.362 / 23.212 | 590 / 810 | 0 / 0 |
| per-axis-zoom1-overlap8 | 3 | 22.612 / 23.073 | 810 / 810 | 0 / 0 |
| per-axis-zoom1-overlap16 | 3 | 22.277 / 23.120 | 810 / 810 | 0 / 0 |
| per-axis-zoom1-overlap8-subdiv4 | 3 | 22.726 / 23.315 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap0 | 3 | 8.537 / 8.635 | 2 / 810 | 0 / 0 |
| per-axis-zoom4-overlap8 | 3 | 23.137 / 25.755 | 803 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16 | 3 | 23.554 / 26.435 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap8-subdiv4 | 3 | 22.741 / 23.549 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16-no-shadows | 3 | 23.332 / 24.619 | 810 / 810 | 0 / 0 |
| per-axis-zoom4-overlap16-unlit | 3 | 24.129 / 26.290 | 810 / 810 | 0 / 0 |

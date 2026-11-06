Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| density-direct-1-timing-off | 3 | 3.523 / 4.036 | 0 / 675 | 12 / 0 |
| density-baked-1-timing-off | 3 | 3.131 / 3.319 | 0 / 675 | 403 / 0 |
| density-baked-2-timing-off | 3 | 3.453 / 4.160 | 0 / 675 | 361 / 0 |
| population-direct-16-overlap0-timing-off | 3 | 5.244 / 5.719 | 0 / 675 | 168 / 0 |
| population-direct-16-overlap1-timing-off | 3 | 5.710 / 6.608 | 0 / 675 | 524 / 0 |
| population-baked-16-overlap0-timing-off | 3 | 8.597 / 9.458 | 0 / 675 | 3296 / 0 |
| population-baked-16-overlap1-timing-off | 3 | 8.497 / 10.064 | 0 / 675 | 11660 / 0 |
| motion-direct-root-timing-off | 3 | 4.107 / 4.299 | 0 / 675 | 12 / 0 |
| motion-direct-parented-timing-off | 3 | 4.152 / 4.938 | 0 / 675 | 12 / 0 |
| motion-baked-root-timing-off | 3 | 3.260 / 4.046 | 0 / 675 | 489 / 0 |
| motion-baked-parented-timing-off | 3 | 3.401 / 4.070 | 0 / 675 | 498 / 0 |

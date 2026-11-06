Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| density-direct-1-timing-on | 3 | 4.302 / 4.765 | 0 / 675 | 12 / 0 |
| density-baked-1-timing-on | 3 | 3.708 / 4.144 | 0 / 675 | 403 / 0 |
| density-baked-2-timing-on | 3 | 3.696 / 4.318 | 0 / 675 | 361 / 0 |
| population-direct-16-overlap0-timing-on | 3 | 6.261 / 7.420 | 0 / 675 | 168 / 0 |
| population-direct-16-overlap1-timing-on | 3 | 6.932 / 7.827 | 0 / 675 | 524 / 0 |
| population-baked-16-overlap0-timing-on | 3 | 9.133 / 10.441 | 0 / 675 | 3296 / 0 |
| population-baked-16-overlap1-timing-on | 3 | 9.866 / 11.403 | 0 / 675 | 11660 / 0 |
| motion-direct-root-timing-on | 3 | 4.911 / 5.577 | 0 / 675 | 12 / 0 |
| motion-direct-parented-timing-on | 3 | 4.912 / 5.543 | 0 / 675 | 12 / 0 |
| motion-baked-root-timing-on | 3 | 3.663 / 4.243 | 0 / 675 | 489 / 0 |
| motion-baked-parented-timing-on | 3 | 3.658 / 4.076 | 0 / 675 | 498 / 0 |

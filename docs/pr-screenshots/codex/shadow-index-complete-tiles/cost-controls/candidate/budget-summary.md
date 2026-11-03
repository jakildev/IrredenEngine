Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| span18-count1 | 3 | 8.836 / 9.683 | 6 / 621 | 0 / 0 |
| span128-count1 | 3 | 8.732 / 9.448 | 6 / 621 | 0 / 0 |
| span512-count1 | 3 | 19.064 / 27.924 | 138 / 621 | 0 / 0 |
| span18-count65 | 3 | 8.559 / 9.403 | 6 / 621 | 0 / 0 |

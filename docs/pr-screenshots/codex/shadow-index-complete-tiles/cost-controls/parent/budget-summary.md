Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| span18-count1 | 3 | 8.416 / 8.975 | 6 / 621 | 0 / 0 |
| span128-count1 | 3 | 8.429 / 8.978 | 6 / 621 | 0 / 0 |
| span512-count1 | 3 | 18.346 / 26.282 | 190 / 621 | 0 / 0 |
| span18-count65 | 3 | 8.389 / 17.164 | 7 / 621 | 0 / 0 |

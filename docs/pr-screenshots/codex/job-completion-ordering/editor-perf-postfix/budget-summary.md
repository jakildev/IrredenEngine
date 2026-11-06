Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| yaw30-zoom4-surface0-timing1 | 3 | 18.836 / 20.916 | 208 / 810 | 12 / 0 |
| yaw30-zoom4-surface1-timing1 | 3 | 21.243 / 22.186 | 331 / 810 | 12 / 0 |
| yaw30-zoom4-surface0-timing0 | 3 | 21.598 / 22.259 | 527 / 810 | 12 / 0 |
| yaw30-zoom4-surface1-timing0 | 3 | 21.963 / 22.762 | 666 / 810 | 12 / 0 |
| yaw67-zoom8-surface0-timing1 | 3 | 21.406 / 22.079 | 643 / 810 | 15 / 0 |
| yaw67-zoom8-surface1-timing1 | 3 | 21.226 / 21.920 | 428 / 810 | 15 / 0 |
| yaw67-zoom8-surface0-timing0 | 3 | 21.433 / 22.097 | 611 / 810 | 15 / 0 |
| yaw67-zoom8-surface1-timing0 | 3 | 21.882 / 22.558 | 752 / 810 | 15 / 0 |

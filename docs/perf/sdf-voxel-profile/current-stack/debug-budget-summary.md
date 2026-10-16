Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| direct-yaw0-shadows-on-timing-on | 3 | 18.169 / 18.685 | 66 / 675 | 0 / 0 |
| baked-yaw0-shadows-on-timing-on | 3 | 9.241 / 18.004 | 24 / 675 | 0 / 0 |
| direct-yaw0-shadows-on-timing-off | 3 | 17.383 / 18.677 | 45 / 675 | 0 / 0 |
| baked-yaw0-shadows-on-timing-off | 3 | 16.502 / 19.334 | 33 / 675 | 0 / 0 |
| direct-yaw0-shadows-off-timing-on | 3 | 8.732 / 9.012 | 1 / 675 | 0 / 0 |
| baked-yaw0-shadows-off-timing-on | 3 | 8.825 / 9.130 | 5 / 675 | 0 / 0 |
| direct-yaw0-shadows-off-timing-off | 3 | 8.793 / 9.236 | 2 / 675 | 0 / 0 |
| baked-yaw0-shadows-off-timing-off | 3 | 8.813 / 9.039 | 2 / 675 | 0 / 0 |
| direct-yaw45-shadows-on-timing-on | 3 | 21.536 / 22.059 | 598 / 675 | 12 / 0 |
| baked-yaw45-shadows-on-timing-on | 3 | 20.253 / 21.118 | 284 / 675 | 403 / 0 |
| direct-yaw45-shadows-on-timing-off | 3 | 20.813 / 21.290 | 440 / 675 | 12 / 0 |
| baked-yaw45-shadows-on-timing-off | 3 | 20.285 / 20.817 | 316 / 675 | 403 / 0 |
| direct-yaw45-shadows-off-timing-on | 3 | 21.008 / 21.777 | 406 / 675 | 12 / 0 |
| baked-yaw45-shadows-off-timing-on | 3 | 19.087 / 20.329 | 51 / 675 | 403 / 0 |
| direct-yaw45-shadows-off-timing-off | 3 | 19.396 / 20.218 | 363 / 675 | 12 / 0 |
| baked-yaw45-shadows-off-timing-off | 3 | 20.106 / 20.851 | 429 / 675 | 403 / 0 |

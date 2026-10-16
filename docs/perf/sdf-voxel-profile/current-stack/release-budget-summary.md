Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| direct-yaw0-shadows-on-timing-on | 3 | 18.265 / 19.027 | 89 / 675 | 0 / 0 |
| baked-yaw0-shadows-on-timing-on | 3 | 9.002 / 9.170 | 1 / 675 | 0 / 0 |
| direct-yaw0-shadows-on-timing-off | 3 | 9.040 / 18.671 | 24 / 675 | 0 / 0 |
| baked-yaw0-shadows-on-timing-off | 3 | 8.953 / 9.343 | 2 / 675 | 0 / 0 |
| direct-yaw0-shadows-off-timing-on | 3 | 8.734 / 8.969 | 1 / 675 | 0 / 0 |
| baked-yaw0-shadows-off-timing-on | 3 | 8.945 / 9.177 | 2 / 675 | 0 / 0 |
| direct-yaw0-shadows-off-timing-off | 3 | 8.822 / 9.179 | 2 / 675 | 0 / 0 |
| baked-yaw0-shadows-off-timing-off | 3 | 8.957 / 9.212 | 3 / 675 | 0 / 0 |
| direct-yaw45-shadows-on-timing-on | 3 | 21.625 / 22.033 | 485 / 675 | 12 / 0 |
| baked-yaw45-shadows-on-timing-on | 3 | 20.796 / 21.337 | 413 / 675 | 403 / 0 |
| direct-yaw45-shadows-on-timing-off | 3 | 20.868 / 21.378 | 409 / 675 | 12 / 0 |
| baked-yaw45-shadows-on-timing-off | 3 | 20.679 / 21.364 | 494 / 675 | 403 / 0 |
| direct-yaw45-shadows-off-timing-on | 3 | 21.100 / 21.670 | 461 / 675 | 12 / 0 |
| baked-yaw45-shadows-off-timing-on | 3 | 20.168 / 21.071 | 259 / 675 | 403 / 0 |
| direct-yaw45-shadows-off-timing-off | 3 | 19.852 / 20.646 | 261 / 675 | 12 / 0 |
| baked-yaw45-shadows-off-timing-off | 3 | 19.812 / 20.423 | 278 / 675 | 403 / 0 |

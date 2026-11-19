| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 287.880 | 287.880–287.880 | n/a |
| frame p95 | 314.110 | 314.110–314.110 | n/a |
| frame p99 | 333.280 | 333.280–333.280 | n/a |
| steady frame avg | 287.660 | 287.660–287.660 | n/a |
| steady frame p95 | 314.110 | 314.110–314.110 | n/a |
| steady frame p99 | 329.790 | 329.790–329.790 | n/a |
| frame split INPUT | 0.049 | 0.049–0.049 | n/a |
| frame split UPDATE | 278.498 | 278.498–278.498 | n/a |
| frame split RENDER | 8.212 | 8.212–8.212 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.124 | 1.124–1.124 | n/a |
| input_to_present mean | 99.309 | 99.309–99.309 | n/a |
| input_to_present p99 | 110.830 | 110.830–110.830 | n/a |
| cursor_to_present mean | 64.236 | 64.236–64.236 | n/a |
| cursor_to_present p99 | 69.962 | 69.962–69.962 | n/a |
| frame_start_to_submit mean | 287.490 | 287.490–287.490 | n/a |
| frame_start_to_submit p99 | 329.763 | 329.763–329.763 | n/a |
| submit_to_present mean | 56.191 | 56.191–56.191 | n/a |
| submit_to_present p99 | 62.061 | 62.061–62.061 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 287.659 | 314.109 | 329.794 | 333.279 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.1552 |

Render device counters: unsupported by this backend.

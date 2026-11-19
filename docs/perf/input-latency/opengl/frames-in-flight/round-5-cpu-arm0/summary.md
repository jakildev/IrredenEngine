| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 348.090 | 348.090–348.090 | n/a |
| frame p95 | 378.640 | 378.640–378.640 | n/a |
| frame p99 | 402.090 | 402.090–402.090 | n/a |
| steady frame avg | 348.740 | 348.740–348.740 | n/a |
| steady frame p95 | 378.640 | 378.640–378.640 | n/a |
| steady frame p99 | 401.560 | 401.560–401.560 | n/a |
| frame split INPUT | 0.069 | 0.069–0.069 | n/a |
| frame split UPDATE | 290.106 | 290.106–290.106 | n/a |
| frame split RENDER | 8.477 | 8.477–8.477 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 49.440 | 49.440–49.440 | n/a |
| input_to_present mean | 93.808 | 93.808–93.808 | n/a |
| input_to_present p99 | 109.055 | 109.055–109.055 | n/a |
| cursor_to_present mean | 57.831 | 57.831–57.831 | n/a |
| cursor_to_present p99 | 69.706 | 69.706–69.706 | n/a |
| frame_start_to_submit mean | 298.977 | 298.977–298.977 | n/a |
| frame_start_to_submit p99 | 344.593 | 344.593–344.593 | n/a |
| submit_to_present mean | 49.633 | 49.633–49.633 | n/a |
| submit_to_present p99 | 61.462 | 61.462–61.462 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 348.741 | 378.639 | 401.564 | 426.630 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0822 |

Render device counters: unsupported by this backend.

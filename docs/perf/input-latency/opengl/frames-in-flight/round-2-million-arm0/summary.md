| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.530 | 32.530–32.530 | n/a |
| frame p95 | 33.630 | 33.630–33.630 | n/a |
| frame p99 | 34.630 | 34.630–34.630 | n/a |
| steady frame avg | 32.490 | 32.490–32.490 | n/a |
| steady frame p95 | 33.340 | 33.340–33.340 | n/a |
| steady frame p99 | 34.110 | 34.110–34.110 | n/a |
| frame split INPUT | 0.021 | 0.021–0.021 | n/a |
| frame split UPDATE | 0.103 | 0.103–0.103 | n/a |
| frame split RENDER | 1.564 | 1.564–1.564 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 30.837 | 30.837–30.837 | n/a |
| input_to_present mean | 32.257 | 32.257–32.257 | n/a |
| input_to_present p99 | 33.919 | 33.919–33.919 | n/a |
| cursor_to_present mean | 32.198 | 32.198–32.198 | n/a |
| cursor_to_present p99 | 33.841 | 33.841–33.841 | n/a |
| frame_start_to_submit mean | 1.715 | 1.715–1.715 | n/a |
| frame_start_to_submit p99 | 2.517 | 2.517–2.517 | n/a |
| submit_to_present mean | 30.655 | 30.655–30.655 | n/a |
| submit_to_present p99 | 32.117 | 32.117–32.117 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 32.490 | 33.343 | 34.110 | 34.516 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.00 | 2.00–2.00 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.1055 |

Render device counters: unsupported by this backend.

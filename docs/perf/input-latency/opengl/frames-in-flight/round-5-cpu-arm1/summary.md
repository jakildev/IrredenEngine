| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 294.700 | 294.700–294.700 | n/a |
| frame p95 | 328.440 | 328.440–328.440 | n/a |
| frame p99 | 355.270 | 355.270–355.270 | n/a |
| steady frame avg | 294.350 | 294.350–294.350 | n/a |
| steady frame p95 | 327.360 | 327.360–327.360 | n/a |
| steady frame p99 | 354.540 | 354.540–354.540 | n/a |
| frame split INPUT | 0.054 | 0.054–0.054 | n/a |
| frame split UPDATE | 285.117 | 285.117–285.117 | n/a |
| frame split RENDER | 8.395 | 8.395–8.395 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.129 | 1.129–1.129 | n/a |
| input_to_present mean | 96.663 | 96.663–96.663 | n/a |
| input_to_present p99 | 114.681 | 114.681–114.681 | n/a |
| cursor_to_present mean | 60.488 | 60.488–60.488 | n/a |
| cursor_to_present p99 | 71.563 | 71.563–71.563 | n/a |
| frame_start_to_submit mean | 294.082 | 294.082–294.082 | n/a |
| frame_start_to_submit p99 | 354.506 | 354.506–354.506 | n/a |
| submit_to_present mean | 52.277 | 52.277–52.277 | n/a |
| submit_to_present p99 | 62.377 | 62.377–62.377 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 294.355 | 327.361 | 354.543 | 373.381 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.3782 |

Render device counters: unsupported by this backend.

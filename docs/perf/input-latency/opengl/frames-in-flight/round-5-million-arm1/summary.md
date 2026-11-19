| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 30.640 | 30.640–30.640 | n/a |
| frame p95 | 32.070 | 32.070–32.070 | n/a |
| frame p99 | 32.470 | 32.470–32.470 | n/a |
| steady frame avg | 30.840 | 30.840–30.840 | n/a |
| steady frame p95 | 32.100 | 32.100–32.100 | n/a |
| steady frame p99 | 32.390 | 32.390–32.390 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.100 | 0.100–0.100 | n/a |
| frame split RENDER | 1.603 | 1.603–1.603 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 28.920 | 28.920–28.920 | n/a |
| input_to_present mean | 61.465 | 61.465–61.465 | n/a |
| input_to_present p99 | 63.442 | 63.442–63.442 | n/a |
| cursor_to_present mean | 61.394 | 61.394–61.394 | n/a |
| cursor_to_present p99 | 63.379 | 63.379–63.379 | n/a |
| frame_start_to_submit mean | 1.823 | 1.823–1.823 | n/a |
| frame_start_to_submit p99 | 2.708 | 2.708–2.708 | n/a |
| submit_to_present mean | 59.751 | 59.751–59.751 | n/a |
| submit_to_present p99 | 61.880 | 61.880–61.880 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 30.844 | 32.096 | 32.386 | 32.500 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.3954 |

Render device counters: unsupported by this backend.

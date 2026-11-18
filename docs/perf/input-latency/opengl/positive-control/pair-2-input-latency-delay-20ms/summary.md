| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 33.220 | 33.220–33.220 | n/a |
| frame p95 | 46.340 | 46.340–46.340 | n/a |
| frame p99 | 48.960 | 48.960–48.960 | n/a |
| steady frame avg | 32.920 | 32.920–32.920 | n/a |
| steady frame p95 | 46.240 | 46.240–46.240 | n/a |
| steady frame p99 | 47.610 | 47.610–47.610 | n/a |
| frame split INPUT | 0.037 | 0.037–0.037 | n/a |
| frame split UPDATE | 0.783 | 0.783–0.783 | n/a |
| frame split RENDER | 3.503 | 3.503–3.503 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 28.892 | 28.892–28.892 | n/a |
| input_to_present mean | 39.062 | 39.062–39.062 | n/a |
| input_to_present p99 | 54.548 | 54.548–54.548 | n/a |
| cursor_to_present mean | 9.907 | 9.907–9.907 | n/a |
| cursor_to_present p99 | 13.249 | 13.249–13.249 | n/a |
| frame_start_to_submit mean | 32.820 | 32.820–32.820 | n/a |
| frame_start_to_submit p99 | 47.560 | 47.560–47.560 | n/a |
| submit_to_present mean | 6.686 | 6.686–6.686 | n/a |
| submit_to_present p99 | 9.954 | 9.954–9.954 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 32.921 | 46.236 | 47.615 | 47.927 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.00 | 2.00–2.00 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.5182 |

Render device counters: unsupported by this backend.

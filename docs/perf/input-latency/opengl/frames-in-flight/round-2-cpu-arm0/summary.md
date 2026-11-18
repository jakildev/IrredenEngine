| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 343.470 | 343.470–343.470 | n/a |
| frame p95 | 375.970 | 375.970–375.970 | n/a |
| frame p99 | 398.250 | 398.250–398.250 | n/a |
| steady frame avg | 346.100 | 346.100–346.100 | n/a |
| steady frame p95 | 376.100 | 376.100–376.100 | n/a |
| steady frame p99 | 398.250 | 398.250–398.250 | n/a |
| frame split INPUT | 0.066 | 0.066–0.066 | n/a |
| frame split UPDATE | 281.867 | 281.867–281.867 | n/a |
| frame split RENDER | 8.329 | 8.329–8.329 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 53.203 | 53.203–53.203 | n/a |
| input_to_present mean | 96.813 | 96.813–96.813 | n/a |
| input_to_present p99 | 115.968 | 115.968–115.968 | n/a |
| cursor_to_present mean | 61.514 | 61.514–61.514 | n/a |
| cursor_to_present p99 | 72.980 | 72.980–72.980 | n/a |
| frame_start_to_submit mean | 292.627 | 292.627–292.627 | n/a |
| frame_start_to_submit p99 | 342.964 | 342.964–342.964 | n/a |
| submit_to_present mean | 53.345 | 53.345–53.345 | n/a |
| submit_to_present p99 | 62.662 | 62.662–62.662 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 346.097 | 376.099 | 398.250 | 416.482 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.1837 |

Render device counters: unsupported by this backend.

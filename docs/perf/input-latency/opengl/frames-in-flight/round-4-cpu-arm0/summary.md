| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 347.830 | 347.830–347.830 | n/a |
| frame p95 | 386.130 | 386.130–386.130 | n/a |
| frame p99 | 404.650 | 404.650–404.650 | n/a |
| steady frame avg | 349.530 | 349.530–349.530 | n/a |
| steady frame p95 | 386.240 | 386.240–386.240 | n/a |
| steady frame p99 | 404.650 | 404.650–404.650 | n/a |
| frame split INPUT | 0.069 | 0.069–0.069 | n/a |
| frame split UPDATE | 289.578 | 289.578–289.578 | n/a |
| frame split RENDER | 8.519 | 8.519–8.519 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 49.664 | 49.664–49.664 | n/a |
| input_to_present mean | 93.856 | 93.856–93.856 | n/a |
| input_to_present p99 | 109.163 | 109.163–109.163 | n/a |
| cursor_to_present mean | 57.651 | 57.651–57.651 | n/a |
| cursor_to_present p99 | 67.615 | 67.615–67.615 | n/a |
| frame_start_to_submit mean | 300.072 | 300.072–300.072 | n/a |
| frame_start_to_submit p99 | 354.961 | 354.961–354.961 | n/a |
| submit_to_present mean | 49.327 | 49.327–49.327 | n/a |
| submit_to_present p99 | 60.113 | 60.113–60.113 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 349.530 | 386.242 | 404.645 | 440.086 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.08 |

Render device counters: unsupported by this backend.

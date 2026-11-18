| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 292.490 | 292.490–292.490 | n/a |
| frame p95 | 324.320 | 324.320–324.320 | n/a |
| frame p99 | 347.610 | 347.610–347.610 | n/a |
| steady frame avg | 292.650 | 292.650–292.650 | n/a |
| steady frame p95 | 324.320 | 324.320–324.320 | n/a |
| steady frame p99 | 341.770 | 341.770–341.770 | n/a |
| frame split INPUT | 0.053 | 0.053–0.053 | n/a |
| frame split UPDATE | 282.968 | 282.968–282.968 | n/a |
| frame split RENDER | 8.338 | 8.338–8.338 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.128 | 1.128–1.128 | n/a |
| input_to_present mean | 100.002 | 100.002–100.002 | n/a |
| input_to_present p99 | 114.636 | 114.636–114.636 | n/a |
| cursor_to_present mean | 64.277 | 64.277–64.277 | n/a |
| cursor_to_present p99 | 72.045 | 72.045–72.045 | n/a |
| frame_start_to_submit mean | 292.601 | 292.601–292.601 | n/a |
| frame_start_to_submit p99 | 341.725 | 341.725–341.725 | n/a |
| submit_to_present mean | 56.171 | 56.171–56.171 | n/a |
| submit_to_present p99 | 63.286 | 63.286–63.286 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 292.650 | 324.320 | 341.769 | 347.608 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.0874 |

Render device counters: unsupported by this backend.

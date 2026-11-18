| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 290.850 | 290.850–290.850 | n/a |
| frame p95 | 321.620 | 321.620–321.620 | n/a |
| frame p99 | 339.390 | 339.390–339.390 | n/a |
| steady frame avg | 287.730 | 287.730–287.730 | n/a |
| steady frame p95 | 314.680 | 314.680–314.680 | n/a |
| steady frame p99 | 327.920 | 327.920–327.920 | n/a |
| frame split INPUT | 0.050 | 0.050–0.050 | n/a |
| frame split UPDATE | 281.475 | 281.475–281.475 | n/a |
| frame split RENDER | 8.230 | 8.230–8.230 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.094 | 1.094–1.094 | n/a |
| input_to_present mean | 98.880 | 98.880–98.880 | n/a |
| input_to_present p99 | 109.454 | 109.454–109.454 | n/a |
| cursor_to_present mean | 63.888 | 63.888–63.888 | n/a |
| cursor_to_present p99 | 71.085 | 71.085–71.085 | n/a |
| frame_start_to_submit mean | 287.733 | 287.733–287.733 | n/a |
| frame_start_to_submit p99 | 327.889 | 327.889–327.889 | n/a |
| submit_to_present mean | 55.968 | 55.968–55.968 | n/a |
| submit_to_present p99 | 62.250 | 62.250–62.250 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 287.730 | 314.681 | 327.922 | 346.397 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.0895 |

Render device counters: unsupported by this backend.

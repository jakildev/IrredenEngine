| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 344.090 | 344.090–344.090 | n/a |
| frame p95 | 369.210 | 369.210–369.210 | n/a |
| frame p99 | 381.560 | 381.560–381.560 | n/a |
| steady frame avg | 345.960 | 345.960–345.960 | n/a |
| steady frame p95 | 369.220 | 369.220–369.220 | n/a |
| steady frame p99 | 379.840 | 379.840–379.840 | n/a |
| frame split INPUT | 0.063 | 0.063–0.063 | n/a |
| frame split UPDATE | 279.936 | 279.936–279.936 | n/a |
| frame split RENDER | 8.091 | 8.091–8.091 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 56.002 | 56.002–56.002 | n/a |
| input_to_present mean | 99.387 | 99.387–99.387 | n/a |
| input_to_present p99 | 111.522 | 111.522–111.522 | n/a |
| cursor_to_present mean | 64.219 | 64.219–64.219 | n/a |
| cursor_to_present p99 | 70.785 | 70.785–70.785 | n/a |
| frame_start_to_submit mean | 289.526 | 289.526–289.526 | n/a |
| frame_start_to_submit p99 | 323.919 | 323.919–323.919 | n/a |
| submit_to_present mean | 56.310 | 56.310–56.310 | n/a |
| submit_to_present p99 | 62.011 | 62.011–62.011 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 345.958 | 369.225 | 379.842 | 441.656 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0803 |

Render device counters: unsupported by this backend.

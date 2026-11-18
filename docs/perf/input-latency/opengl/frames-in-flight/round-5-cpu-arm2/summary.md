| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 292.680 | 292.680–292.680 | n/a |
| frame p95 | 322.970 | 322.970–322.970 | n/a |
| frame p99 | 343.740 | 343.740–343.740 | n/a |
| steady frame avg | 292.260 | 292.260–292.260 | n/a |
| steady frame p95 | 319.670 | 319.670–319.670 | n/a |
| steady frame p99 | 341.040 | 341.040–341.040 | n/a |
| frame split INPUT | 0.052 | 0.052–0.052 | n/a |
| frame split UPDATE | 283.067 | 283.067–283.067 | n/a |
| frame split RENDER | 8.328 | 8.328–8.328 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.237 | 1.237–1.237 | n/a |
| input_to_present mean | 90.750 | 90.750–90.750 | n/a |
| input_to_present p99 | 110.512 | 110.512–110.512 | n/a |
| cursor_to_present mean | 55.189 | 55.189–55.189 | n/a |
| cursor_to_present p99 | 76.257 | 76.257–76.257 | n/a |
| frame_start_to_submit mean | 292.212 | 292.212–292.212 | n/a |
| frame_start_to_submit p99 | 340.998 | 340.998–340.998 | n/a |
| submit_to_present mean | 47.059 | 47.059–47.059 | n/a |
| submit_to_present p99 | 68.397 | 68.397–68.397 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 292.261 | 319.666 | 341.037 | 360.501 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.1288 |

Render device counters: unsupported by this backend.

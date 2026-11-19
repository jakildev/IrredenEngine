| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 298.390 | 298.390–298.390 | n/a |
| frame p95 | 340.720 | 340.720–340.720 | n/a |
| frame p99 | 365.130 | 365.130–365.130 | n/a |
| steady frame avg | 299.360 | 299.360–299.360 | n/a |
| steady frame p95 | 343.780 | 343.780–343.780 | n/a |
| steady frame p99 | 365.130 | 365.130–365.130 | n/a |
| frame split INPUT | 0.050 | 0.050–0.050 | n/a |
| frame split UPDATE | 288.864 | 288.864–288.864 | n/a |
| frame split RENDER | 8.349 | 8.349–8.349 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.123 | 1.123–1.123 | n/a |
| input_to_present mean | 100.516 | 100.516–100.516 | n/a |
| input_to_present p99 | 113.960 | 113.960–113.960 | n/a |
| cursor_to_present mean | 64.161 | 64.161–64.161 | n/a |
| cursor_to_present p99 | 70.699 | 70.699–70.699 | n/a |
| frame_start_to_submit mean | 299.356 | 299.356–299.356 | n/a |
| frame_start_to_submit p99 | 365.103 | 365.103–365.103 | n/a |
| submit_to_present mean | 55.952 | 55.952–55.952 | n/a |
| submit_to_present p99 | 61.222 | 61.222–61.222 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 299.360 | 343.776 | 365.132 | 370.196 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.0608 |

Render device counters: unsupported by this backend.

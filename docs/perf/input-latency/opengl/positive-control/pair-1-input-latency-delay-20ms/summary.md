| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.090 | 34.090–34.090 | n/a |
| frame p95 | 46.750 | 46.750–46.750 | n/a |
| frame p99 | 48.210 | 48.210–48.210 | n/a |
| steady frame avg | 33.410 | 33.410–33.410 | n/a |
| steady frame p95 | 46.600 | 46.600–46.600 | n/a |
| steady frame p99 | 47.490 | 47.490–47.490 | n/a |
| frame split INPUT | 0.039 | 0.039–0.039 | n/a |
| frame split UPDATE | 0.812 | 0.812–0.812 | n/a |
| frame split RENDER | 3.688 | 3.688–3.688 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.550 | 29.550–29.550 | n/a |
| input_to_present mean | 39.923 | 39.923–39.923 | n/a |
| input_to_present p99 | 56.008 | 56.008–56.008 | n/a |
| cursor_to_present mean | 10.444 | 10.444–10.444 | n/a |
| cursor_to_present p99 | 13.615 | 13.615–13.615 | n/a |
| frame_start_to_submit mean | 33.383 | 33.383–33.383 | n/a |
| frame_start_to_submit p99 | 47.455 | 47.455–47.455 | n/a |
| submit_to_present mean | 7.012 | 7.012–7.012 | n/a |
| submit_to_present p99 | 10.564 | 10.564–10.564 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 33.413 | 46.603 | 47.493 | 48.083 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.00 | 2.00–2.00 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 1.2959 |

Render device counters: unsupported by this backend.

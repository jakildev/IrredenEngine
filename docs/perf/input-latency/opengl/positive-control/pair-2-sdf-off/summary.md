| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 2.740 | 2.740–2.740 | n/a |
| frame p95 | 3.770 | 3.770–3.770 | n/a |
| frame p99 | 5.040 | 5.040–5.040 | n/a |
| steady frame avg | 2.430 | 2.430–2.430 | n/a |
| steady frame p95 | 3.500 | 3.500–3.500 | n/a |
| steady frame p99 | 4.370 | 4.370–4.370 | n/a |
| frame split INPUT | 0.004 | 0.004–0.004 | n/a |
| frame split UPDATE | 0.068 | 0.068–0.068 | n/a |
| frame split RENDER | 2.554 | 2.554–2.554 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.112 | 0.112–0.112 | n/a |
| input_to_present mean | 10.160 | 10.160–10.160 | n/a |
| input_to_present p99 | 18.563 | 18.563–18.563 | n/a |
| cursor_to_present mean | 2.778 | 2.778–2.778 | n/a |
| cursor_to_present p99 | 4.657 | 4.657–4.657 | n/a |
| frame_start_to_submit mean | 2.359 | 2.359–2.359 | n/a |
| frame_start_to_submit p99 | 4.316 | 4.316–4.316 | n/a |
| submit_to_present mean | 0.493 | 0.493–0.493 | n/a |
| submit_to_present p99 | 0.863 | 0.863–0.863 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 2.427 | 3.502 | 4.368 | 4.830 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.20 | 0.20–0.20 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.0927 |

Render device counters: unsupported by this backend.

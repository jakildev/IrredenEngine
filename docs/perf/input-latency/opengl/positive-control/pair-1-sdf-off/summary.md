| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 2.890 | 2.890–2.890 | n/a |
| frame p95 | 4.010 | 4.010–4.010 | n/a |
| frame p99 | 5.040 | 5.040–5.040 | n/a |
| steady frame avg | 2.560 | 2.560–2.560 | n/a |
| steady frame p95 | 3.780 | 3.780–3.780 | n/a |
| steady frame p99 | 4.750 | 4.750–4.750 | n/a |
| frame split INPUT | 0.004 | 0.004–0.004 | n/a |
| frame split UPDATE | 0.071 | 0.071–0.071 | n/a |
| frame split RENDER | 2.704 | 2.704–2.704 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.114 | 0.114–0.114 | n/a |
| input_to_present mean | 10.430 | 10.430–10.430 | n/a |
| input_to_present p99 | 19.638 | 19.638–19.638 | n/a |
| cursor_to_present mean | 3.087 | 3.087–3.087 | n/a |
| cursor_to_present p99 | 5.020 | 5.020–5.020 | n/a |
| frame_start_to_submit mean | 2.496 | 2.496–2.496 | n/a |
| frame_start_to_submit p99 | 4.709 | 4.709–4.709 | n/a |
| submit_to_present mean | 0.670 | 0.670–0.670 | n/a |
| submit_to_present p99 | 1.828 | 1.828–1.828 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 2.561 | 3.777 | 4.753 | 4.836 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.20 | 0.20–0.20 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.0847 |

Render device counters: unsupported by this backend.

| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 2.830 | 2.830–2.830 | n/a |
| frame p95 | 4.090 | 4.090–4.090 | n/a |
| frame p99 | 6.530 | 6.530–6.530 | n/a |
| steady frame avg | 2.270 | 2.270–2.270 | n/a |
| steady frame p95 | 2.900 | 2.900–2.900 | n/a |
| steady frame p99 | 3.250 | 3.250–3.250 | n/a |
| frame split INPUT | 0.004 | 0.004–0.004 | n/a |
| frame split UPDATE | 0.072 | 0.072–0.072 | n/a |
| frame split RENDER | 2.643 | 2.643–2.643 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.111 | 0.111–0.111 | n/a |
| input_to_present mean | 10.204 | 10.204–10.204 | n/a |
| input_to_present p99 | 18.498 | 18.498–18.498 | n/a |
| cursor_to_present mean | 2.646 | 2.646–2.646 | n/a |
| cursor_to_present p99 | 3.589 | 3.589–3.589 | n/a |
| frame_start_to_submit mean | 2.204 | 2.204–2.204 | n/a |
| frame_start_to_submit p99 | 3.161 | 3.161–3.161 | n/a |
| submit_to_present mean | 0.512 | 0.512–0.512 | n/a |
| submit_to_present p99 | 0.855 | 0.855–0.855 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 2.266 | 2.896 | 3.246 | 4.092 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.20 | 0.20–0.20 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.1918 |

Render device counters: unsupported by this backend.

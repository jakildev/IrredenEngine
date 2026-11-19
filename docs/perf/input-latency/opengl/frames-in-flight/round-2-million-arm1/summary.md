| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.230 | 31.230–31.230 | n/a |
| frame p95 | 32.180 | 32.180–32.180 | n/a |
| frame p99 | 32.490 | 32.490–32.490 | n/a |
| steady frame avg | 31.230 | 31.230–31.230 | n/a |
| steady frame p95 | 32.150 | 32.150–32.150 | n/a |
| steady frame p99 | 32.430 | 32.430–32.430 | n/a |
| frame split INPUT | 0.021 | 0.021–0.021 | n/a |
| frame split UPDATE | 0.101 | 0.101–0.101 | n/a |
| frame split RENDER | 1.597 | 1.597–1.597 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.507 | 29.507–29.507 | n/a |
| input_to_present mean | 62.250 | 62.250–62.250 | n/a |
| input_to_present p99 | 64.129 | 64.129–64.129 | n/a |
| cursor_to_present mean | 62.180 | 62.180–62.180 | n/a |
| cursor_to_present p99 | 64.074 | 64.074–64.074 | n/a |
| frame_start_to_submit mean | 1.779 | 1.779–1.779 | n/a |
| frame_start_to_submit p99 | 2.691 | 2.691–2.691 | n/a |
| submit_to_present mean | 60.582 | 60.582–60.582 | n/a |
| submit_to_present p99 | 62.432 | 62.432–62.432 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 31.233 | 32.146 | 32.432 | 32.831 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.4451 |

Render device counters: unsupported by this backend.

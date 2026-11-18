| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.010 | 31.010–31.010 | n/a |
| frame p95 | 32.190 | 32.190–32.190 | n/a |
| frame p99 | 32.530 | 32.530–32.530 | n/a |
| steady frame avg | 31.240 | 31.240–31.240 | n/a |
| steady frame p95 | 32.220 | 32.220–32.220 | n/a |
| steady frame p99 | 32.530 | 32.530–32.530 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.100 | 0.100–0.100 | n/a |
| frame split RENDER | 1.623 | 1.623–1.623 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.263 | 29.263–29.263 | n/a |
| input_to_present mean | 93.525 | 93.525–93.525 | n/a |
| input_to_present p99 | 95.369 | 95.369–95.369 | n/a |
| cursor_to_present mean | 93.448 | 93.448–93.448 | n/a |
| cursor_to_present p99 | 95.314 | 95.314–95.314 | n/a |
| frame_start_to_submit mean | 1.864 | 1.864–1.864 | n/a |
| frame_start_to_submit p99 | 3.440 | 3.440–3.440 | n/a |
| submit_to_present mean | 91.779 | 91.779–91.779 | n/a |
| submit_to_present p99 | 93.696 | 93.696–93.696 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 31.239 | 32.218 | 32.532 | 32.715 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 223 / 225 | 2 | 0.1239 |

Render device counters: unsupported by this backend.

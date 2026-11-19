| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 30.990 | 30.990–30.990 | n/a |
| frame p95 | 31.920 | 31.920–31.920 | n/a |
| frame p99 | 32.400 | 32.400–32.400 | n/a |
| steady frame avg | 30.960 | 30.960–30.960 | n/a |
| steady frame p95 | 31.880 | 31.880–31.880 | n/a |
| steady frame p99 | 32.400 | 32.400–32.400 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.100 | 0.100–0.100 | n/a |
| frame split RENDER | 1.564 | 1.564–1.564 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.303 | 29.303–29.303 | n/a |
| input_to_present mean | 61.702 | 61.702–61.702 | n/a |
| input_to_present p99 | 63.295 | 63.295–63.295 | n/a |
| cursor_to_present mean | 61.630 | 61.630–61.630 | n/a |
| cursor_to_present p99 | 63.244 | 63.244–63.244 | n/a |
| frame_start_to_submit mean | 1.769 | 1.769–1.769 | n/a |
| frame_start_to_submit p99 | 2.714 | 2.714–2.714 | n/a |
| submit_to_present mean | 60.047 | 60.047–60.047 | n/a |
| submit_to_present p99 | 61.624 | 61.624–61.624 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 30.955 | 31.878 | 32.396 | 32.815 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.1355 |

Render device counters: unsupported by this backend.

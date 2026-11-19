| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.150 | 31.150–31.150 | n/a |
| frame p95 | 32.070 | 32.070–32.070 | n/a |
| frame p99 | 32.460 | 32.460–32.460 | n/a |
| steady frame avg | 31.170 | 31.170–31.170 | n/a |
| steady frame p95 | 32.070 | 32.070–32.070 | n/a |
| steady frame p99 | 32.460 | 32.460–32.460 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.100 | 0.100–0.100 | n/a |
| frame split RENDER | 1.578 | 1.578–1.578 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.452 | 29.452–29.452 | n/a |
| input_to_present mean | 93.341 | 93.341–93.341 | n/a |
| input_to_present p99 | 95.153 | 95.153–95.153 | n/a |
| cursor_to_present mean | 93.272 | 93.272–93.272 | n/a |
| cursor_to_present p99 | 95.098 | 95.098–95.098 | n/a |
| frame_start_to_submit mean | 1.778 | 1.778–1.778 | n/a |
| frame_start_to_submit p99 | 2.827 | 2.827–2.827 | n/a |
| submit_to_present mean | 91.673 | 91.673–91.673 | n/a |
| submit_to_present p99 | 93.460 | 93.460–93.460 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 31.169 | 32.066 | 32.464 | 32.737 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 223 / 225 | 2 | 0.1348 |

Render device counters: unsupported by this backend.

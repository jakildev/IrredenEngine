| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 30.420 | 30.420–30.420 | n/a |
| frame p95 | 32.700 | 32.700–32.700 | n/a |
| frame p99 | 33.410 | 33.410–33.410 | n/a |
| steady frame avg | 30.130 | 30.130–30.130 | n/a |
| steady frame p95 | 31.280 | 31.280–31.280 | n/a |
| steady frame p99 | 32.310 | 32.310–32.310 | n/a |
| frame split INPUT | 0.019 | 0.019–0.019 | n/a |
| frame split UPDATE | 0.098 | 0.098–0.098 | n/a |
| frame split RENDER | 1.564 | 1.564–1.564 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 28.744 | 28.744–28.744 | n/a |
| input_to_present mean | 29.913 | 29.913–29.913 | n/a |
| input_to_present p99 | 32.095 | 32.095–32.095 | n/a |
| cursor_to_present mean | 29.845 | 29.845–29.845 | n/a |
| cursor_to_present p99 | 32.014 | 32.014–32.014 | n/a |
| frame_start_to_submit mean | 1.733 | 1.733–1.733 | n/a |
| frame_start_to_submit p99 | 2.828 | 2.828–2.828 | n/a |
| submit_to_present mean | 28.277 | 28.277–28.277 | n/a |
| submit_to_present p99 | 30.470 | 30.470–30.470 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 30.135 | 31.281 | 32.306 | 32.934 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.7757 |

Render device counters: unsupported by this backend.

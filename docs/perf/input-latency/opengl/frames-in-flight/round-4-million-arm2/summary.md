| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.220 | 31.220–31.220 | n/a |
| frame p95 | 32.070 | 32.070–32.070 | n/a |
| frame p99 | 32.390 | 32.390–32.390 | n/a |
| steady frame avg | 31.190 | 31.190–31.190 | n/a |
| steady frame p95 | 32.050 | 32.050–32.050 | n/a |
| steady frame p99 | 32.350 | 32.350–32.350 | n/a |
| frame split INPUT | 0.019 | 0.019–0.019 | n/a |
| frame split UPDATE | 0.100 | 0.100–0.100 | n/a |
| frame split RENDER | 1.555 | 1.555–1.555 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.541 | 29.541–29.541 | n/a |
| input_to_present mean | 93.405 | 93.405–93.405 | n/a |
| input_to_present p99 | 95.955 | 95.955–95.955 | n/a |
| cursor_to_present mean | 93.336 | 93.336–93.336 | n/a |
| cursor_to_present p99 | 95.894 | 95.894–95.894 | n/a |
| frame_start_to_submit mean | 1.720 | 1.720–1.720 | n/a |
| frame_start_to_submit p99 | 2.388 | 2.388–2.388 | n/a |
| submit_to_present mean | 91.790 | 91.790–91.790 | n/a |
| submit_to_present p99 | 94.374 | 94.374–94.374 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 31.191 | 32.048 | 32.347 | 33.942 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 223 / 225 | 2 | 0.072 |

Render device counters: unsupported by this backend.

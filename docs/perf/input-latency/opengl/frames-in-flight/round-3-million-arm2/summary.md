| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.120 | 31.120–31.120 | n/a |
| frame p95 | 31.920 | 31.920–31.920 | n/a |
| frame p99 | 32.330 | 32.330–32.330 | n/a |
| steady frame avg | 31.150 | 31.150–31.150 | n/a |
| steady frame p95 | 31.930 | 31.930–31.930 | n/a |
| steady frame p99 | 32.330 | 32.330–32.330 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.097 | 0.097–0.097 | n/a |
| frame split RENDER | 1.540 | 1.540–1.540 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.459 | 29.459–29.459 | n/a |
| input_to_present mean | 93.253 | 93.253–93.253 | n/a |
| input_to_present p99 | 94.835 | 94.835–94.835 | n/a |
| cursor_to_present mean | 93.188 | 93.188–93.188 | n/a |
| cursor_to_present p99 | 94.758 | 94.758–94.758 | n/a |
| frame_start_to_submit mean | 1.727 | 1.727–1.727 | n/a |
| frame_start_to_submit p99 | 2.565 | 2.565–2.565 | n/a |
| submit_to_present mean | 91.629 | 91.629–91.629 | n/a |
| submit_to_present p99 | 92.870 | 92.870–92.870 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 31.146 | 31.932 | 32.326 | 32.618 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 223 / 225 | 2 | 0.1044 |

Render device counters: unsupported by this backend.

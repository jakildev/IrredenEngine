| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.180 | 31.180–31.180 | n/a |
| frame p95 | 32.120 | 32.120–32.120 | n/a |
| frame p99 | 32.480 | 32.480–32.480 | n/a |
| steady frame avg | 31.170 | 31.170–31.170 | n/a |
| steady frame p95 | 32.150 | 32.150–32.150 | n/a |
| steady frame p99 | 32.480 | 32.480–32.480 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.101 | 0.101–0.101 | n/a |
| frame split RENDER | 1.601 | 1.601–1.601 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.460 | 29.460–29.460 | n/a |
| input_to_present mean | 93.308 | 93.308–93.308 | n/a |
| input_to_present p99 | 95.172 | 95.172–95.172 | n/a |
| cursor_to_present mean | 93.240 | 93.240–93.240 | n/a |
| cursor_to_present p99 | 95.130 | 95.130–95.130 | n/a |
| frame_start_to_submit mean | 1.788 | 1.788–1.788 | n/a |
| frame_start_to_submit p99 | 2.989 | 2.989–2.989 | n/a |
| submit_to_present mean | 91.628 | 91.628–91.628 | n/a |
| submit_to_present p99 | 93.461 | 93.461–93.461 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 31.167 | 32.146 | 32.481 | 32.972 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 223 / 225 | 2 | 0.133 |

Render device counters: unsupported by this backend.

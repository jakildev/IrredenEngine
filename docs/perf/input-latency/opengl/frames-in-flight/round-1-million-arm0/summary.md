| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.130 | 32.130–32.130 | n/a |
| frame p95 | 33.380 | 33.380–33.380 | n/a |
| frame p99 | 33.980 | 33.980–33.980 | n/a |
| steady frame avg | 32.480 | 32.480–32.480 | n/a |
| steady frame p95 | 33.460 | 33.460–33.460 | n/a |
| steady frame p99 | 33.980 | 33.980–33.980 | n/a |
| frame split INPUT | 0.022 | 0.022–0.022 | n/a |
| frame split UPDATE | 0.121 | 0.121–0.121 | n/a |
| frame split RENDER | 1.594 | 1.594–1.594 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 30.392 | 30.392–30.392 | n/a |
| input_to_present mean | 32.229 | 32.229–32.229 | n/a |
| input_to_present p99 | 33.566 | 33.566–33.566 | n/a |
| cursor_to_present mean | 32.152 | 32.152–32.152 | n/a |
| cursor_to_present p99 | 33.439 | 33.439–33.439 | n/a |
| frame_start_to_submit mean | 1.822 | 1.822–1.822 | n/a |
| frame_start_to_submit p99 | 2.789 | 2.789–2.789 | n/a |
| submit_to_present mean | 30.538 | 30.538–30.538 | n/a |
| submit_to_present p99 | 31.549 | 31.549–31.549 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 32.476 | 33.464 | 33.977 | 34.240 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.1125 |

Render device counters: unsupported by this backend.

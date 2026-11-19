| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.490 | 32.490–32.490 | n/a |
| frame p95 | 33.570 | 33.570–33.570 | n/a |
| frame p99 | 34.020 | 34.020–34.020 | n/a |
| steady frame avg | 32.510 | 32.510–32.510 | n/a |
| steady frame p95 | 33.570 | 33.570–33.570 | n/a |
| steady frame p99 | 33.860 | 33.860–33.860 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.103 | 0.103–0.103 | n/a |
| frame split RENDER | 1.585 | 1.585–1.585 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 30.781 | 30.781–30.781 | n/a |
| input_to_present mean | 32.268 | 32.268–32.268 | n/a |
| input_to_present p99 | 33.595 | 33.595–33.595 | n/a |
| cursor_to_present mean | 32.203 | 32.203–32.203 | n/a |
| cursor_to_present p99 | 33.482 | 33.482–33.482 | n/a |
| frame_start_to_submit mean | 1.749 | 1.749–1.749 | n/a |
| frame_start_to_submit p99 | 2.618 | 2.618–2.618 | n/a |
| submit_to_present mean | 30.635 | 30.635–30.635 | n/a |
| submit_to_present p99 | 31.560 | 31.560–31.560 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 32.506 | 33.570 | 33.856 | 34.077 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.00 | 2.00–2.00 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.3386 |

Render device counters: unsupported by this backend.

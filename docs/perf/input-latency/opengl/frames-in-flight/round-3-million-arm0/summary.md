| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.560 | 32.560–32.560 | n/a |
| frame p95 | 33.580 | 33.580–33.580 | n/a |
| frame p99 | 34.200 | 34.200–34.200 | n/a |
| steady frame avg | 32.560 | 32.560–32.560 | n/a |
| steady frame p95 | 33.540 | 33.540–33.540 | n/a |
| steady frame p99 | 34.200 | 34.200–34.200 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.102 | 0.102–0.102 | n/a |
| frame split RENDER | 1.551 | 1.551–1.551 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 30.886 | 30.886–30.886 | n/a |
| input_to_present mean | 32.329 | 32.329–32.329 | n/a |
| input_to_present p99 | 33.833 | 33.833–33.833 | n/a |
| cursor_to_present mean | 32.262 | 32.262–32.262 | n/a |
| cursor_to_present p99 | 33.657 | 33.657–33.657 | n/a |
| frame_start_to_submit mean | 1.749 | 1.749–1.749 | n/a |
| frame_start_to_submit p99 | 3.103 | 3.103–3.103 | n/a |
| submit_to_present mean | 30.698 | 30.698–30.698 | n/a |
| submit_to_present p99 | 31.855 | 31.855–31.855 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 32.559 | 33.543 | 34.203 | 34.682 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.00 | 2.00–2.00 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0671 |

Render device counters: unsupported by this backend.

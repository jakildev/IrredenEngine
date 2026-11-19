| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.220 | 31.220–31.220 | n/a |
| frame p95 | 32.080 | 32.080–32.080 | n/a |
| frame p99 | 32.470 | 32.470–32.470 | n/a |
| steady frame avg | 31.220 | 31.220–31.220 | n/a |
| steady frame p95 | 31.980 | 31.980–31.980 | n/a |
| steady frame p99 | 32.250 | 32.250–32.250 | n/a |
| frame split INPUT | 0.019 | 0.019–0.019 | n/a |
| frame split UPDATE | 0.100 | 0.100–0.100 | n/a |
| frame split RENDER | 1.558 | 1.558–1.558 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.541 | 29.541–29.541 | n/a |
| input_to_present mean | 62.241 | 62.241–62.241 | n/a |
| input_to_present p99 | 63.515 | 63.515–63.515 | n/a |
| cursor_to_present mean | 62.174 | 62.174–62.174 | n/a |
| cursor_to_present p99 | 63.432 | 63.432–63.432 | n/a |
| frame_start_to_submit mean | 1.723 | 1.723–1.723 | n/a |
| frame_start_to_submit p99 | 2.669 | 2.669–2.669 | n/a |
| submit_to_present mean | 60.623 | 60.623–60.623 | n/a |
| submit_to_present p99 | 61.916 | 61.916–61.916 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 31.217 | 31.981 | 32.247 | 32.565 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.3566 |

Render device counters: unsupported by this backend.

| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 344.910 | 344.910–344.910 | n/a |
| frame p95 | 371.500 | 371.500–371.500 | n/a |
| frame p99 | 393.370 | 393.370–393.370 | n/a |
| steady frame avg | 346.470 | 346.470–346.470 | n/a |
| steady frame p95 | 372.090 | 372.090–372.090 | n/a |
| steady frame p99 | 393.370 | 393.370–393.370 | n/a |
| frame split INPUT | 0.064 | 0.064–0.064 | n/a |
| frame split UPDATE | 283.593 | 283.593–283.593 | n/a |
| frame split RENDER | 8.081 | 8.081–8.081 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 53.169 | 53.169–53.169 | n/a |
| input_to_present mean | 96.664 | 96.664–96.664 | n/a |
| input_to_present p99 | 114.435 | 114.435–114.435 | n/a |
| cursor_to_present mean | 60.723 | 60.723–60.723 | n/a |
| cursor_to_present p99 | 71.789 | 71.789–71.789 | n/a |
| frame_start_to_submit mean | 293.582 | 293.582–293.582 | n/a |
| frame_start_to_submit p99 | 345.478 | 345.478–345.478 | n/a |
| submit_to_present mean | 52.769 | 52.769–52.769 | n/a |
| submit_to_present p99 | 62.908 | 62.908–62.908 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 346.474 | 372.085 | 393.368 | 418.415 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.3615 |

Render device counters: unsupported by this backend.

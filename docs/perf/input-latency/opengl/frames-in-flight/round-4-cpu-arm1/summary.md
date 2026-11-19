| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 290.220 | 290.220–290.220 | n/a |
| frame p95 | 316.030 | 316.030–316.030 | n/a |
| frame p99 | 337.670 | 337.670–337.670 | n/a |
| steady frame avg | 290.110 | 290.110–290.110 | n/a |
| steady frame p95 | 314.730 | 314.730–314.730 | n/a |
| steady frame p99 | 327.910 | 327.910–327.910 | n/a |
| frame split INPUT | 0.053 | 0.053–0.053 | n/a |
| frame split UPDATE | 280.835 | 280.835–280.835 | n/a |
| frame split RENDER | 8.256 | 8.256–8.256 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.074 | 1.074–1.074 | n/a |
| input_to_present mean | 91.096 | 91.096–91.096 | n/a |
| input_to_present p99 | 105.353 | 105.353–105.353 | n/a |
| cursor_to_present mean | 55.790 | 55.790–55.790 | n/a |
| cursor_to_present p99 | 68.304 | 68.304–68.304 | n/a |
| frame_start_to_submit mean | 290.111 | 290.111–290.111 | n/a |
| frame_start_to_submit p99 | 327.882 | 327.882–327.882 | n/a |
| submit_to_present mean | 47.703 | 47.703–47.703 | n/a |
| submit_to_present p99 | 60.785 | 60.785–60.785 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 290.110 | 314.733 | 327.913 | 354.632 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.0747 |

Render device counters: unsupported by this backend.

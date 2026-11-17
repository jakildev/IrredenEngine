| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 28.820 | 28.820–28.820 | n/a |
| frame p95 | 28.790 | 28.790–28.790 | n/a |
| frame p99 | 29.910 | 29.910–29.910 | n/a |
| steady frame avg | 27.830 | 27.830–27.830 | n/a |
| steady frame p95 | 28.790 | 28.790–28.790 | n/a |
| steady frame p99 | 29.210 | 29.210–29.210 | n/a |
| frame split INPUT | 0.004 | 0.004–0.004 | n/a |
| frame split UPDATE | 3.364 | 3.364–3.364 | n/a |
| frame split RENDER | 5.209 | 5.209–5.209 | n/a |
| frame split GPU | 18.794 | 18.794–18.794 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 1.446 | 1.446–1.446 | n/a |
| GPU frame commandBufferSpans | 18.794 | 18.794–18.794 | n/a |
| GPU frame envelope | 18.794 | 18.794–18.794 | n/a |
| input_to_present mean | 26.297 | 26.297–26.297 | n/a |
| input_to_present p99 | 26.940 | 26.940–26.940 | n/a |
| cursor_to_present mean | 24.334 | 24.334–24.334 | n/a |
| cursor_to_present p99 | 24.820 | 24.820–24.820 | n/a |
| frame_start_to_submit mean | 8.495 | 8.495–8.495 | n/a |
| frame_start_to_submit p99 | 9.920 | 9.920–9.920 | n/a |
| submit_to_present mean | 19.188 | 19.188–19.188 | n/a |
| submit_to_present p99 | 19.644 | 19.644–19.644 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 27.828 | 28.789 | 29.212 | 29.911 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0015 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 3 | 3.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 41943040 | 41943040.0 |
| bufferStagedPatches | 6.000 | 6.000–6.000 | 9 | 1803.0 |
| bufferStagedBytes | 16015776.000 | 16015776.000–16015776.000 | 36140776 | 4824857800.0 |
| bufferFullReplacements | 13.000 | 13.000–13.000 | 14 | 3901.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 313.7 | 313.7–313.7 |

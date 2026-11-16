| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 29.910 | 29.910–29.910 | n/a |
| frame p95 | 32.770 | 32.770–32.770 | n/a |
| frame p99 | 33.610 | 33.610–33.610 | n/a |
| steady frame avg | 29.800 | 29.800–29.800 | n/a |
| steady frame p95 | 32.910 | 32.910–32.910 | n/a |
| steady frame p99 | 33.430 | 33.430–33.430 | n/a |
| frame split INPUT | 0.088 | 0.088–0.088 | n/a |
| frame split UPDATE | 1.176 | 1.176–1.176 | n/a |
| frame split RENDER | 2.147 | 2.147–2.147 | n/a |
| frame split GPU | 4.546 | 4.546–4.546 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 21.955 | 21.955–21.955 | n/a |
| GPU frame commandBufferSpans | 4.546 | 4.546–4.546 | n/a |
| GPU frame envelope | 4.546 | 4.546–4.546 | n/a |
| input_to_present mean | 28.824 | 28.824–28.824 | n/a |
| input_to_present p99 | 31.748 | 31.748–31.748 | n/a |
| cursor_to_present mean | 7.230 | 7.230–7.230 | n/a |
| cursor_to_present p99 | 10.010 | 10.010–10.010 | n/a |
| submit_to_present mean | 5.057 | 5.057–5.057 | n/a |
| submit_to_present p99 | 6.253 | 6.253–6.253 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 29.799 | 32.910 | 33.433 | 33.609 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0017 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 1 | 1.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 4194304 | 4194304.0 |
| bufferStagedPatches | 12.000 | 12.000–12.000 | 14 | 3602.0 |
| bufferStagedBytes | 219264.000 | 219264.000–219264.000 | 282804 | 65842740.0 |
| bufferFullReplacements | 15.000 | 15.000–15.000 | 15 | 4500.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 182.9 | 182.9–182.9 |

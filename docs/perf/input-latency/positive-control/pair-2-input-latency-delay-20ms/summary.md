| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 29.580 | 29.580–29.580 | n/a |
| frame p95 | 33.240 | 33.240–33.240 | n/a |
| frame p99 | 34.430 | 34.430–34.430 | n/a |
| steady frame avg | 29.480 | 29.480–29.480 | n/a |
| steady frame p95 | 33.200 | 33.200–33.200 | n/a |
| steady frame p99 | 33.910 | 33.910–33.910 | n/a |
| frame split INPUT | 0.085 | 0.085–0.085 | n/a |
| frame split UPDATE | 1.154 | 1.154–1.154 | n/a |
| frame split RENDER | 2.166 | 2.166–2.166 | n/a |
| frame split GPU | 4.224 | 4.224–4.224 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 21.954 | 21.954–21.954 | n/a |
| GPU frame commandBufferSpans | 4.224 | 4.224–4.224 | n/a |
| GPU frame envelope | 4.224 | 4.224–4.224 | n/a |
| input_to_present mean | 28.503 | 28.503–28.503 | n/a |
| input_to_present p99 | 32.056 | 32.056–32.056 | n/a |
| cursor_to_present mean | 6.847 | 6.847–6.847 | n/a |
| cursor_to_present p99 | 9.858 | 9.858–9.858 | n/a |
| submit_to_present mean | 4.656 | 4.656–4.656 | n/a |
| submit_to_present p99 | 6.500 | 6.500–6.500 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 29.479 | 33.201 | 33.911 | 34.821 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0027 |

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

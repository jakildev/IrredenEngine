| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.480 | 8.480–8.480 | n/a |
| frame p95 | 8.930 | 8.930–8.930 | n/a |
| frame p99 | 11.390 | 11.390–11.390 | n/a |
| steady frame avg | 8.440 | 8.440–8.440 | n/a |
| steady frame p95 | 8.930 | 8.930–8.930 | n/a |
| steady frame p99 | 10.440 | 10.440–10.440 | n/a |
| frame split INPUT | 0.022 | 0.022–0.022 | n/a |
| frame split UPDATE | 0.374 | 0.374–0.374 | n/a |
| frame split RENDER | 1.537 | 1.537–1.537 | n/a |
| frame split GPU | 4.070 | 4.070–4.070 | n/a |
| frame split Pacing | 1.768 | 1.768–1.768 | n/a |
| frame split Remainder | 0.709 | 0.709–0.709 | n/a |
| GPU frame commandBufferSpans | 4.070 | 4.070–4.070 | n/a |
| GPU frame envelope | 4.070 | 4.070–4.070 | n/a |
| input_to_present mean | 10.982 | 10.982–10.982 | n/a |
| input_to_present p99 | 17.148 | 17.148–17.148 | n/a |
| cursor_to_present mean | 6.362 | 6.362–6.362 | n/a |
| cursor_to_present p99 | 8.192 | 8.192–8.192 | n/a |
| frame_start_to_submit mean | 2.356 | 2.356–2.356 | n/a |
| frame_start_to_submit p99 | 6.207 | 6.207–6.207 | n/a |
| submit_to_present mean | 4.545 | 4.545–4.545 | n/a |
| submit_to_present p99 | 6.274 | 6.274–6.274 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 8.441 | 8.931 | 10.440 | 11.562 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.001 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 1 | 1.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 4194304 | 4194304.0 |
| bufferStagedPatches | 11.013 | 11.013–11.013 | 14 | 3308.0 |
| bufferStagedBytes | 111243.733 | 111243.733–111243.733 | 282804 | 33655620.0 |
| bufferFullReplacements | 15.000 | 15.000–15.000 | 15 | 4500.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 182.9 | 182.9–182.9 |

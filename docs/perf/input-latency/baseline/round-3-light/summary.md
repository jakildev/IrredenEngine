| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.420 | 8.420–8.420 | n/a |
| frame p95 | 8.510 | 8.510–8.510 | n/a |
| frame p99 | 8.700 | 8.700–8.700 | n/a |
| steady frame avg | 8.330 | 8.330–8.330 | n/a |
| steady frame p95 | 8.460 | 8.460–8.460 | n/a |
| steady frame p99 | 8.570 | 8.570–8.570 | n/a |
| frame split INPUT | 0.019 | 0.019–0.019 | n/a |
| frame split UPDATE | 0.220 | 0.220–0.220 | n/a |
| frame split RENDER | 1.056 | 1.056–1.056 | n/a |
| frame split GPU | 4.354 | 4.354–4.354 | n/a |
| frame split Pacing | 2.097 | 2.097–2.097 | n/a |
| frame split Remainder | 0.678 | 0.678–0.678 | n/a |
| GPU frame commandBufferSpans | 4.354 | 4.354–4.354 | n/a |
| GPU frame envelope | 4.354 | 4.354–4.354 | n/a |
| input_to_present mean | 10.167 | 10.167–10.167 | n/a |
| input_to_present p99 | 15.448 | 15.448–15.448 | n/a |
| cursor_to_present mean | 5.843 | 5.843–5.843 | n/a |
| cursor_to_present p99 | 7.301 | 7.301–7.301 | n/a |
| frame_start_to_submit mean | 1.188 | 1.188–1.188 | n/a |
| frame_start_to_submit p99 | 4.164 | 4.164–4.164 | n/a |
| submit_to_present mean | 4.910 | 4.910–4.910 | n/a |
| submit_to_present p99 | 6.084 | 6.084–6.084 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 8.332 | 8.456 | 8.567 | 8.638 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0007 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 1 | 1.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 4194304 | 4194304.0 |
| bufferStagedPatches | 11.004 | 11.004–11.004 | 14 | 3304.0 |
| bufferStagedBytes | 110270.578 | 110270.578–110270.578 | 282804 | 33217700.0 |
| bufferFullReplacements | 15.000 | 15.000–15.000 | 15 | 4500.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 182.9 | 182.9–182.9 |

| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.420 | 8.420–8.420 | n/a |
| frame p95 | 8.650 | 8.650–8.650 | n/a |
| frame p99 | 9.360 | 9.360–9.360 | n/a |
| steady frame avg | 8.360 | 8.360–8.360 | n/a |
| steady frame p95 | 8.710 | 8.710–8.710 | n/a |
| steady frame p99 | 9.310 | 9.310–9.310 | n/a |
| frame split INPUT | 0.018 | 0.018–0.018 | n/a |
| frame split UPDATE | 0.205 | 0.205–0.205 | n/a |
| frame split RENDER | 1.042 | 1.042–1.042 | n/a |
| frame split GPU | 3.613 | 3.613–3.613 | n/a |
| frame split Pacing | 3.087 | 3.087–3.087 | n/a |
| frame split Remainder | 0.453 | 0.453–0.453 | n/a |
| GPU frame commandBufferSpans | 3.613 | 3.613–3.613 | n/a |
| GPU frame envelope | 3.613 | 3.613–3.613 | n/a |
| input_to_present mean | 9.608 | 9.608–9.608 | n/a |
| input_to_present p99 | 17.209 | 17.209–17.209 | n/a |
| cursor_to_present mean | 5.151 | 5.151–5.151 | n/a |
| cursor_to_present p99 | 8.047 | 8.047–8.047 | n/a |
| submit_to_present mean | 3.900 | 3.900–3.900 | n/a |
| submit_to_present p99 | 5.916 | 5.916–5.916 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 8.363 | 8.713 | 9.311 | 9.385 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0008 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 1 | 1.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 4194304 | 4194304.0 |
| bufferStagedPatches | 11.004 | 11.004–11.004 | 14 | 3306.0 |
| bufferStagedBytes | 110270.578 | 110270.578–110270.578 | 282804 | 33436660.0 |
| bufferFullReplacements | 15.000 | 15.000–15.000 | 15 | 4500.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 182.9 | 182.9–182.9 |

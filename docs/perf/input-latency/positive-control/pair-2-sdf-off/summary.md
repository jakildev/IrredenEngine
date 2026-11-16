| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.450 | 8.450–8.450 | n/a |
| frame p95 | 8.820 | 8.820–8.820 | n/a |
| frame p99 | 10.120 | 10.120–10.120 | n/a |
| steady frame avg | 8.380 | 8.380–8.380 | n/a |
| steady frame p95 | 8.820 | 8.820–8.820 | n/a |
| steady frame p99 | 9.820 | 9.820–9.820 | n/a |
| frame split INPUT | 0.027 | 0.027–0.027 | n/a |
| frame split UPDATE | 0.271 | 0.271–0.271 | n/a |
| frame split RENDER | 1.255 | 1.255–1.255 | n/a |
| frame split GPU | 4.037 | 4.037–4.037 | n/a |
| frame split Pacing | 2.163 | 2.163–2.163 | n/a |
| frame split Remainder | 0.692 | 0.692–0.692 | n/a |
| GPU frame commandBufferSpans | 4.037 | 4.037–4.037 | n/a |
| GPU frame envelope | 4.037 | 4.037–4.037 | n/a |
| input_to_present mean | 10.007 | 10.007–10.007 | n/a |
| input_to_present p99 | 17.562 | 17.562–17.562 | n/a |
| cursor_to_present mean | 5.568 | 5.568–5.568 | n/a |
| cursor_to_present p99 | 8.624 | 8.624–8.624 | n/a |
| submit_to_present mean | 4.166 | 4.166–4.166 | n/a |
| submit_to_present p99 | 6.163 | 6.163–6.163 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 8.383 | 8.819 | 9.825 | 10.116 |

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

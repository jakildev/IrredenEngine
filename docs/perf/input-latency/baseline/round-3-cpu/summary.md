| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 14.730 | 14.730–14.730 | n/a |
| frame p95 | 19.390 | 19.390–19.390 | n/a |
| frame p99 | 38.180 | 38.180–38.180 | n/a |
| steady frame avg | 13.840 | 13.840–13.840 | n/a |
| steady frame p95 | 19.150 | 19.150–19.150 | n/a |
| steady frame p99 | 20.270 | 20.270–20.270 | n/a |
| frame split INPUT | 0.002 | 0.002–0.002 | n/a |
| frame split UPDATE | 6.673 | 6.673–6.673 | n/a |
| frame split RENDER | 3.222 | 3.222–3.222 | n/a |
| frame split GPU | 3.767 | 3.767–3.767 | n/a |
| frame split Pacing | 0.257 | 0.257–0.257 | n/a |
| frame split Remainder | 0.805 | 0.805–0.805 | n/a |
| GPU frame commandBufferSpans | 3.767 | 3.767–3.767 | n/a |
| GPU frame envelope | 3.767 | 3.767–3.767 | n/a |
| input_to_present mean | 15.674 | 15.674–15.674 | n/a |
| input_to_present p99 | 24.699 | 24.699–24.699 | n/a |
| cursor_to_present mean | 7.161 | 7.161–7.161 | n/a |
| cursor_to_present p99 | 12.517 | 12.517–12.517 | n/a |
| frame_start_to_submit mean | 9.422 | 9.422–9.422 | n/a |
| frame_start_to_submit p99 | 11.578 | 11.578–11.578 | n/a |
| submit_to_present mean | 3.980 | 3.980–3.980 | n/a |
| submit_to_present p99 | 9.311 | 9.311–9.311 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 13.843 | 19.151 | 20.265 | 20.892 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0004 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferStagedPatches | 4.000 | 4.000–4.000 | 4 | 1200.0 |
| bufferStagedBytes | 148.000 | 148.000–148.000 | 148 | 44400.0 |
| bufferFullReplacements | 15.000 | 15.000–15.000 | 19 | 4504.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 182.2 | 182.2–182.2 |
